#include "profiles/ProfileStorage.h"
#include "profiles/BuiltinGames.h"
#include "utilities/JsonHelper.h"
#include "utilities/Logger.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <cmath>
#include <sstream>

namespace prismshift {

namespace {

std::wstring WideToUtf8(std::wstring_view wideStr) {
    if (wideStr.empty()) return {};
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wideStr.data(), static_cast<int>(wideStr.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return {};
    std::string utf8(sizeNeeded, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideStr.data(), static_cast<int>(wideStr.size()), utf8.data(), sizeNeeded, nullptr, nullptr);
    int wLen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(wLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), wLen);
    return out;
}

std::optional<std::string> ReadUtf8File(const std::wstring& filePath) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return std::nullopt;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0 ||
        !IsProfileStorageFileSizeAllowed(static_cast<std::uint64_t>(size.QuadPart))) {
        CloseHandle(hFile);
        return std::nullopt;
    }

    std::string contents(static_cast<size_t>(size.QuadPart), '\0');
    DWORD bytesRead = 0;
    bool ok = ReadFile(hFile, contents.data(), static_cast<DWORD>(contents.size()), &bytesRead, nullptr) != FALSE;
    CloseHandle(hFile);
    if (!ok || bytesRead != contents.size()) return std::nullopt;
    return contents;
}

JsonValue ProfileToJson(const ColorProfile& cp) {
    std::map<std::wstring, JsonValue> obj;
    obj[L"id"] = JsonValue(cp.id);
    obj[L"name"] = JsonValue(cp.name);
    obj[L"executablePath"] = JsonValue(cp.executablePath);
    obj[L"enabled"] = JsonValue(cp.enabled);
    obj[L"isCustom"] = JsonValue(cp.isCustom);
    obj[L"gammaR"] = JsonValue(cp.gammaR);
    obj[L"gammaG"] = JsonValue(cp.gammaG);
    obj[L"gammaB"] = JsonValue(cp.gammaB);
    obj[L"brightness"] = JsonValue(cp.brightness);
    obj[L"contrast"] = JsonValue(cp.contrast);
    obj[L"saturation"] = JsonValue(cp.saturation);
    obj[L"vibrance"] = JsonValue(cp.vibrance);
    obj[L"hue"] = JsonValue(cp.hue);
    obj[L"screenshotHotkey"] = JsonValue(cp.screenshotHotkey);
    if (cp.resolutionProfile) {
        std::map<std::wstring, JsonValue> resolution;
        resolution[L"width"] = JsonValue(cp.resolutionProfile->width);
        resolution[L"height"] = JsonValue(cp.resolutionProfile->height);
        const wchar_t* scaling = L"aspect";
        if (cp.resolutionProfile->scaling == ResolutionScaling::FullScreenStretch) {
            scaling = L"stretch";
        } else if (cp.resolutionProfile->scaling == ResolutionScaling::NoScaling) {
            scaling = L"none";
        }
        resolution[L"scaling"] = JsonValue(scaling);
        obj[L"resolution"] = JsonValue(std::move(resolution));
    }

    std::vector<JsonValue> targets;
    for (const auto& target : cp.targetProcesses) targets.emplace_back(target);
    obj[L"targets"] = JsonValue(std::move(targets));
    return JsonValue(std::move(obj));
}

std::optional<ColorProfile> ProfileFromJson(const JsonValue& item) {
    if (!item.IsObject()) return std::nullopt;

    ColorProfile cp;
    if (const auto* val = item.Get(L"id")) cp.id = val->AsString();
    if (const auto* val = item.Get(L"name")) cp.name = val->AsString();
    if (const auto* val = item.Get(L"executablePath")) cp.executablePath = val->AsString();
    if (const auto* val = item.Get(L"enabled")) cp.enabled = val->AsBool(true);
    if (const auto* val = item.Get(L"isCustom")) cp.isCustom = val->AsBool(false);
    if (const auto* val = item.Get(L"gammaR")) cp.gammaR = val->AsDouble(1.0);
    if (const auto* val = item.Get(L"gammaG")) cp.gammaG = val->AsDouble(1.0);
    if (const auto* val = item.Get(L"gammaB")) cp.gammaB = val->AsDouble(1.0);
    if (const auto* val = item.Get(L"brightness")) cp.brightness = val->AsDouble(1.0);
    if (const auto* val = item.Get(L"contrast")) cp.contrast = val->AsDouble(1.0);
    if (const auto* val = item.Get(L"saturation")) cp.saturation = val->AsDouble(1.0);
    if (const auto* val = item.Get(L"vibrance")) cp.vibrance = val->AsInt(0);
    if (const auto* val = item.Get(L"hue")) cp.hue = val->AsInt(0);
    if (const auto* val = item.Get(L"screenshotHotkey")) cp.screenshotHotkey = val->AsString();
    if (const auto* resolution = item.Get(L"resolution"); resolution) {
        ResolutionProfile profile;
        const auto* width = resolution->Get(L"width");
        const auto* height = resolution->Get(L"height");
        bool valid = resolution->IsObject() &&
                     width && width->IsNumber() &&
                     height && height->IsNumber();
        if (valid) {
            const double widthValue = width->AsDouble();
            const double heightValue = height->AsDouble();
            valid = std::isfinite(widthValue) && std::isfinite(heightValue) &&
                    std::floor(widthValue) == widthValue &&
                    std::floor(heightValue) == heightValue &&
                    widthValue <= 32768.0 && heightValue <= 32768.0;
            if (valid) {
                profile.width = static_cast<int>(widthValue);
                profile.height = static_cast<int>(heightValue);
            }
            if (const auto* scaling = resolution->Get(L"scaling"); scaling) {
                if (!scaling->IsString()) {
                    valid = false;
                } else if (scaling->AsString() == L"aspect") {
                    profile.scaling = ResolutionScaling::MaintainAspectRatio;
                } else if (scaling->AsString() == L"stretch") {
                    profile.scaling = ResolutionScaling::FullScreenStretch;
                } else if (scaling->AsString() == L"none") {
                    profile.scaling = ResolutionScaling::NoScaling;
                } else {
                    valid = false;
                }
            }
            if (valid && profile.IsValid()) {
                cp.resolutionProfile = profile;
            } else {
                Logger::Warn(L"Ignoring invalid resolution profile for game profile: " + cp.id);
            }
        } else {
            Logger::Warn(L"Ignoring incomplete resolution profile for game profile: " + cp.id);
        }
    }
    if (const auto* targets = item.Get(L"targets"); targets && targets->IsArray()) {
        for (const auto& target : targets->AsArray()) {
            if (target.IsString() && !target.AsString().empty()) cp.targetProcesses.push_back(target.AsString());
        }
    }

    cp.RecomputeRamp();
    if (!cp.IsValid()) return std::nullopt;
    return cp;
}

} // namespace

std::wstring ProfileStorage::GetConfigDirectory() {
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        std::wstring dir = std::wstring(appDataPath) + L"\\Lumetra";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    return L".";
}

std::wstring ProfileStorage::GetConfigFilePath() {
    return GetConfigDirectory() + L"\\profiles.json";
}

StorageData ProfileStorage::LoadStorage(const GammaRamp& baselineRamp) {
    std::wstring filePath = GetConfigFilePath();
    StorageData data;
    data.monitorProfile = ColorProfile::CreateMonitorCustomization(baselineRamp);

    DWORD attributes = GetFileAttributesW(filePath.c_str());
    DWORD attributeError = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    if (attributes == INVALID_FILE_ATTRIBUTES &&
        (attributeError == ERROR_FILE_NOT_FOUND || attributeError == ERROR_PATH_NOT_FOUND)) {
        Logger::Info(L"No existing profiles.json found. Creating defaults in: " + filePath);
        data.gameProfiles = BuiltinGames::GetDefaultList();
        SaveStorage(data);
        return data;
    }

    auto contents = ReadUtf8File(filePath);
    if (!contents) {
        Logger::Warn(L"Could not read profiles.json (empty, oversized, or inaccessible). Loading defaults without overwriting it.");
        data.gameProfiles = BuiltinGames::GetDefaultList();
        return data;
    }

    auto parsed = JsonValue::Parse(*contents);
    if (!parsed || !parsed->IsObject()) {
        Logger::Warn(L"Failed to parse profiles.json. Loading defaults.");
        data.gameProfiles = BuiltinGames::GetDefaultList();
        return data;
    }

    // Load Monitor Customization if present
    if (const JsonValue* monVal = parsed->Get(L"monitor")) {
        if (auto monProf = ProfileFromJson(*monVal)) {
            data.monitorProfile = std::move(*monProf);
            data.monitorProfile.id = L"monitor";
            data.monitorProfile.name = L"Monitor Customization";
            data.monitorProfile.isDefault = true;
            data.monitorProfile.enabled = true;
            data.monitorProfile.RecomputeRamp();
            Logger::Info(L"Loaded Monitor Customization profile (Saturation: " +
                         std::to_wstring(data.monitorProfile.saturation) + L", Vibrance: " +
                         std::to_wstring(data.monitorProfile.vibrance) + L").");
        }
    }

    // Load Game Profiles
    const JsonValue* profilesArr = parsed->Get(L"profiles");
    if (profilesArr && profilesArr->IsArray()) {
        for (const auto& item : profilesArr->AsArray()) {
            if (auto profile = ProfileFromJson(item)) {
                // If an old config stored monitor/default in profiles array, load it as monitorProfile
                if (profile->id == L"default" || profile->id == L"monitor") {
                    data.monitorProfile = std::move(*profile);
                    data.monitorProfile.id = L"monitor";
                    data.monitorProfile.name = L"Monitor Customization";
                    data.monitorProfile.isDefault = true;
                    data.monitorProfile.enabled = true;
                    data.monitorProfile.RecomputeRamp();
                } else {
                    data.gameProfiles.push_back(std::move(*profile));
                }
            }
        }
    }

    if (data.gameProfiles.empty()) {
        data.gameProfiles = BuiltinGames::GetDefaultList();
        SaveStorage(data);
    }

    Logger::Info(L"Loaded " + std::to_wstring(data.gameProfiles.size()) + L" game profiles from " + filePath);
    return data;
}

bool ProfileStorage::SaveStorage(const StorageData& data) {
    std::wstring filePath = GetConfigFilePath();
    std::wstring tempPath = filePath + L".tmp";

    std::vector<JsonValue> arr;
    for (const auto& cp : data.gameProfiles) {
        if (cp.isDefault || cp.id == L"monitor" || cp.id == L"default") continue;
        arr.push_back(ProfileToJson(cp));
    }

    std::map<std::wstring, JsonValue> rootObj;
    rootObj[L"monitor"] = ProfileToJson(data.monitorProfile);
    rootObj[L"profiles"] = JsonValue(std::move(arr));
    JsonValue root(std::move(rootObj));

    std::string serialized = root.Serialize(2);
    if (!IsProfileStorageFileSizeAllowed(serialized.size())) {
        Logger::Error(L"Serialized profiles.json exceeds the 4 MiB storage limit.");
        return false;
    }

    HANDLE hFile = CreateFileW(
        tempPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        Logger::Error(L"Failed to create temp file for saving profiles: " + tempPath);
        return false;
    }

    DWORD bytesWritten = 0;
    bool writeOk = WriteFile(hFile, serialized.data(), static_cast<DWORD>(serialized.size()), &bytesWritten, nullptr) != FALSE;
    CloseHandle(hFile);

    if (!writeOk || bytesWritten != serialized.size()) {
        DeleteFileW(tempPath.c_str());
        Logger::Error(L"WriteFile failed while saving profiles.");
        return false;
    }

    if (!MoveFileExW(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        Logger::Error(L"MoveFileExW failed while finalizing profiles.json.");
        return false;
    }

    return true;
}

std::vector<ColorProfile> ProfileStorage::LoadProfiles() {
    return LoadStorage(GammaRamp::CreateIdentity()).gameProfiles;
}

bool ProfileStorage::SaveProfiles(const std::vector<ColorProfile>& profiles) {
    StorageData data;
    data.monitorProfile = ColorProfile::CreateMonitorCustomization(GammaRamp::CreateIdentity());
    data.gameProfiles = profiles;
    return SaveStorage(data);
}

bool ProfileStorage::ExportProfile(const ColorProfile& profile, const std::wstring& filePath) {
    if (!profile.IsValid()) return false;
    std::map<std::wstring, JsonValue> rootObj;
    rootObj[L"profiles"] = JsonValue(std::vector<JsonValue>{ProfileToJson(profile)});
    std::string serialized = JsonValue(std::move(rootObj)).Serialize(2);
    if (!IsProfileStorageFileSizeAllowed(serialized.size())) return false;

    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    return output.good();
}

std::optional<ColorProfile> ProfileStorage::ImportProfile(const std::wstring& filePath) {
    auto contents = ReadUtf8File(filePath);
    if (!contents) return std::nullopt;
    auto parsed = JsonValue::Parse(*contents);
    if (!parsed || !parsed->IsObject()) return std::nullopt;
    const auto* profiles = parsed->Get(L"profiles");
    if (!profiles || !profiles->IsArray() || profiles->AsArray().empty()) return std::nullopt;
    return ProfileFromJson(profiles->AsArray().front());
}

} // namespace prismshift
