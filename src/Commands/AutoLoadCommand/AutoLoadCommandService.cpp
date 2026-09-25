#include "Commands/AutoLoadCommand/AutoLoadCommandService.h"

#include "Commands/AutoLoadCommand/AutoLoadPlanner.h"

#include <algorithm>
#include <vector>

namespace ra_commands::autoload
{
    namespace
    {
        constexpr std::uint64_t MIN_RESERVATION_MS = 5'000;
        constexpr std::uint64_t MAX_RESERVATION_MS = 60'000;

        const Unit* FindUnit(const Snapshot& snapshot, UnitId id)
        {
            const auto it = std::find_if(snapshot.Units.begin(), snapshot.Units.end(),
                [id](const Unit& unit) { return unit.Id == id; });
            return it == snapshot.Units.end() ? nullptr : &*it;
        }

        bool MatchesAddress(const Unit* unit, std::uintptr_t address)
        {
            return unit && (unit->Address == 0 || address == 0 ||
                unit->Address == address);
        }

        void ExcludeUnits(std::vector<UnitId>& ids, const std::vector<UnitId>& excluded)
        {
            std::erase_if(ids, [&excluded](UnitId id)
            {
                return std::find(excluded.begin(), excluded.end(), id) != excluded.end();
            });
        }
    }

    AutoLoadCommandService::AutoLoadCommandService(
        IAutoLoadGamePort& game,
        commands::ClickedMissionDispatcher& dispatcher,
        const std::atomic<bool>& isSafeModeEnabled)
        : mGame(game), mDispatcher(dispatcher), mIsSafeModeEnabled(isSafeModeEnabled)
    {
    }

    void AutoLoadCommandService::OnHotkey()
    {
        if (!mDispatcher.IsSessionActive())
        {
            Reset();
            return;
        }

        if (mEpoch != mDispatcher.Epoch())
        {
            Reset();
            mEpoch = mDispatcher.Epoch();
        }

        Snapshot snapshot;
        if (!mGame.CaptureSnapshot(snapshot))
        {
            return;
        }

        const bool isSafeModeEnabled = mIsSafeModeEnabled.load(std::memory_order_acquire);
        const auto nowMs = isSafeModeEnabled ? mGame.GetCurrentTimeMs() : 0;
        if (isSafeModeEnabled)
        {
            PruneReservations(snapshot, nowMs);
            std::vector<UnitId> excluded;
            for (const auto& reservation : mReservations)
            {
                excluded.push_back(reservation.Transport.Id);
                for (const auto& passenger : reservation.Passengers)
                {
                    excluded.push_back(passenger.Id);
                }
            }
            ExcludeUnits(snapshot.SelectedInfantries, excluded);
            ExcludeUnits(snapshot.SelectedVehicles, excluded);
            ExcludeUnits(snapshot.FriendlyPassengers, excluded);
            ExcludeUnits(snapshot.FriendlyTransports, excluded);
        }

        const auto pairs = isSafeModeEnabled ? PlanSafeMode(snapshot) : Plan(snapshot);
        std::vector<UnitId> deselect;
        LoadReservation reservation;
        if (!pairs.empty())
        {
            reservation.Transport.Id = pairs.front().Transport;
            reservation.StartedAtMs = nowMs;
        }
        for (const auto& pair : pairs)
        {
            commands::ClickedMissionIntent intent;
            if (!mGame.MakeEnterIntent(
                    pair.Passenger, pair.Transport, mDispatcher.Epoch(), intent))
            {
                continue;
            }

            const auto result = mDispatcher.Submit(intent);
            if (result != commands::ClickedMissionEnqueueResult::Enqueued &&
                result != commands::ClickedMissionEnqueueResult::Duplicate)
            {
                continue;
            }

            if (isSafeModeEnabled)
            {
                reservation.Transport.Address = intent.TargetCell
                    ? intent.TargetCell->Address : 0;
                reservation.Passengers.push_back({pair.Passenger, intent.Actor.Address});
            }

            if (std::find(deselect.begin(), deselect.end(), pair.Passenger) == deselect.end())
            {
                deselect.push_back(pair.Passenger);
            }
            if (std::find(deselect.begin(), deselect.end(), pair.Transport) == deselect.end())
            {
                deselect.push_back(pair.Transport);
            }
        }

        for (const auto id : deselect)
        {
            mGame.Deselect(id);
        }
        if (isSafeModeEnabled && !reservation.Passengers.empty())
        {
            mReservations.push_back(std::move(reservation));
        }
    }

    void AutoLoadCommandService::Reset()
    {
        mReservations.clear();
        mEpoch = 0;
    }

    void AutoLoadCommandService::PruneReservations(const Snapshot& snapshot,
        std::uint64_t nowMs)
    {
        std::erase_if(mReservations, [&](const LoadReservation& reservation)
        {
            const Unit* transport = FindUnit(snapshot, reservation.Transport.Id);
            if (!MatchesAddress(transport, reservation.Transport.Address) ||
                !transport->IsInPlayfield || transport->IsInTransport ||
                nowMs < reservation.StartedAtMs)
            {
                return true;
            }

            bool hasPendingPassenger = false;
            bool hasEnteringPassenger = false;
            bool hasPendingIntent = false;
            for (const auto& reservedPassenger : reservation.Passengers)
            {
                const Unit* passenger = FindUnit(snapshot, reservedPassenger.Id);
                if (MatchesAddress(passenger, reservedPassenger.Address) &&
                    passenger->IsInPlayfield && !passenger->IsInTransport)
                {
                    hasPendingPassenger = true;
                    hasEnteringPassenger |= passenger->IsEnteringTransport;
                    hasPendingIntent |= mDispatcher.HasPendingActor(
                        commands::ClickedMissionProducer::AutoLoad,
                        reservedPassenger.Address,
                        static_cast<std::uint32_t>(reservedPassenger.Id));
                }
            }
            if (!hasPendingPassenger)
            {
                return true;
            }

            if (hasPendingIntent)
            {
                return false;
            }

            const auto ageMs = nowMs - reservation.StartedAtMs;
            // 原生事件可能晚于提交生效；任务仍在执行时延长占用，最终超时避免卡住载具。
            return ageMs >= MAX_RESERVATION_MS ||
                (ageMs >= MIN_RESERVATION_MS && !hasEnteringPassenger);
        });
    }
}
