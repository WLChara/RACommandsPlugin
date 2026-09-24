#include "Game/CursorCellAccess.h"

#include "Game/GameObjectAccess.h"

#include <YRPPCore.h>
#include <DisplayClass.h>
#include <MapClass.h>
#include <Unsorted.h>

#include <Windows.h>

namespace ra_commands::game
{
    std::optional<CursorCell> CaptureCursorCell()
    {
        const HWND window = Game::hWnd.get();
        auto* const display = DisplayClass::Instance.get();
        auto* const map = MapClass::Instance.get();
        if (!IsGameSessionReady() || !window || !IsWindow(window) ||
            GetForegroundWindow() != window ||
            !display || !map)
        {
            return std::nullopt;
        }

        POINT cursor{};
        RECT client{};
        if (!GetCursorPos(&cursor) || !ScreenToClient(window, &cursor) ||
            !GetClientRect(window, &client) ||
            cursor.x < client.left || cursor.y < client.top ||
            cursor.x >= client.right || cursor.y >= client.bottom)
        {
            return std::nullopt;
        }

        Point2D clientPoint{cursor.x, cursor.y};
        CellStruct cell{};
        CoordStruct coordinate{};
        ObjectClass* target = nullptr;
        BYTE secondaryFlag = 0;
        BYTE primaryFlag = 0;
        if (!display->ProcessClickCoords(&clientPoint, &cell, &coordinate,
                &target, &secondaryFlag, &primaryFlag) ||
            !map->CoordinatesLegal(cell) ||
            !map->IsWithinUsableArea(cell, false) ||
            !map->TryGetCellAt(cell))
        {
            return std::nullopt;
        }
        return CursorCell{cell.X, cell.Y};
    }
}
