#include "profiles/BuiltinGames.h"

namespace prismshift {

std::vector<ColorProfile> BuiltinGames::GetDefaultList() {
    std::vector<ColorProfile> list;

    // 1. VALORANT
    list.push_back(ColorProfile::CreateGameWithVibrance(
        L"valorant",
        L"VALORANT",
        { L"valorant-win64-shipping.exe", L"valorant.exe" },
        1.22, 1.22, 1.20, // R, G, B gamma
        1.02,             // Brightness
        1.05,             // Contrast
        1.30,             // Saturation (130%)
        25                // Digital Vibrance (+25)
    ));

    // 2. Roblox
    list.push_back(ColorProfile::CreateGameWithVibrance(
        L"roblox",
        L"Roblox",
        { L"robloxplayerbeta.exe", L"robloxplayerlauncher.exe" },
        1.15, 1.15, 1.15,
        1.00,
        1.03,
        1.20,
        15
    ));

    // 3. Counter-Strike 2
    list.push_back(ColorProfile::CreateGameWithVibrance(
        L"cs2",
        L"Counter-Strike 2",
        { L"cs2.exe" },
        1.20, 1.20, 1.20,
        1.00,
        1.05,
        1.35,             // Saturation (135%)
        30                // Digital Vibrance (+30)
    ));

    // 4. Fortnite
    list.push_back(ColorProfile::CreateGameWithVibrance(
        L"fortnite",
        L"Fortnite",
        { L"fortniteclient-win64-shipping.exe" },
        1.18, 1.18, 1.18,
        1.00,
        1.04,
        1.25,
        20
    ));

    // 5. Apex Legends
    list.push_back(ColorProfile::CreateGameWithVibrance(
        L"apex_legends",
        L"Apex Legends",
        { L"r5apex.exe", L"r5apex_dx12.exe" },
        1.20, 1.20, 1.20,
        1.00,
        1.05,
        1.28,
        25
    ));

    // 6. Overwatch 2
    list.push_back(ColorProfile::CreateGame(
        L"overwatch2",
        L"Overwatch 2",
        { L"overwatch.exe" },
        1.15, 1.15, 1.15,
        1.00,
        1.03
    ));

    // 7. Minecraft
    list.push_back(ColorProfile::CreateGame(
        L"minecraft",
        L"Minecraft",
        { L"javaw.exe", L"minecraft.windows.exe", L"minecraft.gfx.test.exe" },
        1.20, 1.20, 1.20,
        1.02,
        1.02
    ));

    // 8. Grand Theft Auto V
    list.push_back(ColorProfile::CreateGame(
        L"gta5",
        L"Grand Theft Auto V",
        { L"gta5.exe", L"gta5_b3095.exe" },
        1.15, 1.15, 1.15,
        1.00,
        1.04
    ));

    return list;
}

} // namespace prismshift
