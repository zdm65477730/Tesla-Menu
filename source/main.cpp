/**
 * Copyright (C) 2020 WerWolv
 * 
 * This file is part of Tesla Menu.
 * 
 * Tesla Menu is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 * 
 * Tesla Menu is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Tesla Menu.  If not, see <http://www.gnu.org/licenses/>.
 */

#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>
#include <filesystem>
#include <tuple>

#include <switch/nro.h>
#include <switch/nacp.h>

#include "logo_bin.h"

using namespace tsl;

constexpr int Module_OverlayLoader  = 348;

constexpr Result ResultSuccess      = MAKERESULT(0, 0);
constexpr Result ResultParseError   = MAKERESULT(Module_OverlayLoader, 1);

std::tuple<Result, std::string, std::string> getOverlayInfo(std::string filePath) {
    FILE *file = fopen(filePath.c_str(), "r");

    NroHeader header;
    NroAssetHeader assetHeader;
    NacpStruct nacp;

    fseek(file, sizeof(NroStart), SEEK_SET);
    if (fread(&header, sizeof(NroHeader), 1, file) != 1) {
        fclose(file);
        return { ResultParseError, "", "" };
    }

    fseek(file, header.size, SEEK_SET);
    if (fread(&assetHeader, sizeof(NroAssetHeader), 1, file) != 1) {
        fclose(file);
        return { ResultParseError, "", "" };
    }

    fseek(file, header.size + assetHeader.nacp.offset, SEEK_SET);
    if (fread(&nacp, sizeof(NacpStruct), 1, file) != 1) {
        fclose(file);
        return { ResultParseError, "", "" };
    }
    
    fclose(file);

    return { ResultSuccess, std::string(nacp.lang[0].name, std::strlen(nacp.lang[0].name)), std::string(nacp.display_version, std::strlen(nacp.display_version)) };
}

static void switchTencentVerToGlobalVer() {
    Result rc;
    constexpr u32 ExosphereEmummcType = 65007;
    u64 is_emummc;
    bool is_do_for_ofw = false;

    std::string cfgFilePath = std::string("sdmc:/config/") + APPTITLE + "/" + "enable_for_ofw.flag";
    if (std::filesystem::exists(cfgFilePath))
        is_do_for_ofw = true;

    if (R_FAILED(rc = splInitialize()))
        fatalThrow(MAKERESULT(Module_HomebrewLoader, R_DESCRIPTION(rc)));

    if (R_FAILED(rc = splGetConfig(static_cast<SplConfigItem>(ExosphereEmummcType), &is_emummc))) {
        splExit();
        fatalThrow(MAKERESULT(Module_HomebrewLoader, R_DESCRIPTION(rc)));
    }
    splExit();

    if (!is_emummc && !is_do_for_ofw)
        return;

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        bool isTencentVersion = false;
        if (R_SUCCEEDED(rc = setsysGetT(&isTencentVersion))) {
            if (isTencentVersion) {
                if (R_SUCCEEDED(rc = setsysSetT(false))) {
                    if (R_SUCCEEDED(rc = setsysSetRegionCode(SetRegion_HTK))) {
                        if (R_SUCCEEDED(rc = spsmInitialize())) {
                            spsmShutdown(true);
                            spsmExit();
                            setsysExit();
                        } else {
                            setsysExit();
                            fatalThrow(MAKERESULT(Module_HomebrewLoader, R_DESCRIPTION(rc)));
                        }
                    } else {
                        setsysExit();
                        fatalThrow(MAKERESULT(Module_HomebrewLoader, R_DESCRIPTION(rc)));
                    }
                } else {
                    setsysExit();
                    fatalThrow(MAKERESULT(Module_HomebrewLoader, R_DESCRIPTION(rc)));
                }
            }
        } else {
            setsysExit();
            fatalThrow(MAKERESULT(Module_HomebrewLoader, R_DESCRIPTION(rc)));
        }
        setsysExit();
    }
}

static tsl::elm::HeaderOverlayFrame *rootFrame = nullptr;
static std::vector<std::string> sortArray{};

static void rebuildUI() {
    auto *overlayList = new tsl::elm::List();  
    auto *header = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
        const u8 *logo = logo_bin;

        for (s32 y1 = 0; y1 < 31; y1++) {
            for (s32 x1 = 0; x1 < 84; x1++) {
                const tsl::Color color = { static_cast<u8>(logo[3] >> 4), static_cast<u8>(logo[2] >> 4), static_cast<u8>(logo[1] >> 4), static_cast<u8>(logo[0] >> 4) };
                renderer->setPixelBlendSrc(20 + x1, 20 + y1, renderer->a(color));
                logo += 4;
            }
        }

        renderer->drawString(envGetLoaderInfo(), false, 20, 68, 15, renderer->a(0xFFFF));
    });

    auto noOverlaysError = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, u16 x, u16 y, u16 w, u16 h) {
        renderer->drawString("\uE150", false, (tsl::cfg::FramebufferWidth - 90) / 2, 300, 90, renderer->a(tsl::style::color::ColorText));
        renderer->drawString("noOverlaysErrorOverlayTeslaMenuCustomDrawerText"_tr.c_str(), false, 50, 380, 25, renderer->a(tsl::style::color::ColorText));
        renderer->drawString("noOverlaysHitOverlayTeslaMenuCustomDrawerText"_tr.c_str(), false, 38, 410, 15, renderer->a(tsl::style::color::ColorDescription));
    });

    std::vector<std::filesystem::directory_entry> overlayFiles;

    for (const auto &entry : std::filesystem::directory_iterator("sdmc:/switch/.overlays")) {
        if (entry.path().filename() == "ovlmenu.ovl")
            continue;

        if (entry.path().extension() != ".ovl")
            continue;

        overlayFiles.push_back(entry);
    }

    std::sort(overlayFiles.begin(), overlayFiles.end(), [](const auto &left, const auto &right) {
        return left.path().filename() < right.path().filename();
    });

    std::vector<std::filesystem::directory_entry> sortedOverlayFiles;
    if (!sortArray.empty()) {
        for (auto item : sortArray) {
            for (auto ovlFileItem = overlayFiles.begin(); ovlFileItem != overlayFiles.end(); ++ovlFileItem) {
                if ((*ovlFileItem).path().filename().string() == (item + ".ovl")) {
                    sortedOverlayFiles.emplace_back(*ovlFileItem);
                    overlayFiles.erase(ovlFileItem);
                    break;
                }
            }
        }
        for (auto ovlFileItem = overlayFiles.begin(); ovlFileItem != overlayFiles.end();) {
            sortedOverlayFiles.emplace_back(*ovlFileItem);
            ovlFileItem = overlayFiles.erase(ovlFileItem);
        }
    }

    if (!sortedOverlayFiles.empty()) {
        overlayFiles.swap(sortedOverlayFiles);
    }

    std::string base_lang;
    tsl::tr::GetSysBaseLanguage(base_lang);
    for (const auto &entry : overlayFiles) {
        auto [result, name, version] = getOverlayInfo(entry.path());
        if (result != ResultSuccess)
            continue;

        std::string pluginName = name;
        std::string pluginLangPath = std::string("sdmc:/switch/.overlays/lang/") + name + "/" + base_lang + ".json";
        if (std::filesystem::exists(pluginLangPath)) {
            try {
                std::ifstream ifs(pluginLangPath);
                auto lang_json = nlohmann::json::parse(ifs);
                if(!lang_json.empty()) {
                    for(auto item : lang_json.items()) {
                        if (!item.key().compare("PluginName")) {
                            pluginName = item.value();
                            break;
                        }
                    }
                }
            } catch(std::exception&) {}
        }

        auto *listEntry = new tsl::elm::ListItem(pluginName);
        listEntry->setValue(version, true);
        listEntry->setClickListener([entry](s64 key) {
            if (key & HidNpadButton_A) {
                tsl::setNextOverlay(entry.path());
                
                tsl::Overlay::get()->close();
                return true;
            }

            return false;
        });

        overlayList->addItem(listEntry);
    }

    rootFrame->setHeader(header);

    if (overlayFiles.empty()) {
        rootFrame->setContent(noOverlaysError);
        delete overlayList;
    } else {
        rootFrame->setContent(overlayList);
    }
}

class GuiMain : public tsl::Gui {
public:
    GuiMain() { }
    ~GuiMain() { }

    tsl::elm::Element* createUI() override {
        rootFrame = new tsl::elm::HeaderOverlayFrame();
        
        rebuildUI();

        return rootFrame;
    }
};

class OverlayTeslaMenu : public tsl::Overlay {
public:
    OverlayTeslaMenu() { }
    ~OverlayTeslaMenu() { }

    virtual void initServices() override {
        fsdevMountSdmc();

        switchTencentVerToGlobalVer();

        std::string jsonStr = R"(
            {
                "noOverlaysErrorOverlayTeslaMenuCustomDrawerText": "No Overlays found!",
                "noOverlaysHitOverlayTeslaMenuCustomDrawerText": "Place your .ovl files in /switch/.overlays"
            }
        )";
        std::string lanPath = std::string("sdmc:/switch/.overlays/lang/") + APPTITLE + "/";
        tsl::tr::InitTrans(lanPath, jsonStr);

        sortArray.clear();
        std::string sortFilePath = std::string("sdmc:/config/") + APPTITLE + "/" + "sort.cfg";
        if (std::filesystem::exists(sortFilePath)) {
            std::ifstream ifs(sortFilePath, std::ifstream::in);
            if(ifs.is_open()) {
                std::string strPluginName;
                while(std::getline(ifs, strPluginName)) {
                    sortArray.emplace_back(strPluginName);
                }
                ifs.close();
            }
        }
    }

    virtual void exitServices() override {
        fsdevUnmountDevice("sdmc");
    }

    void onShow() override { 
        if (rootFrame != nullptr) {
            tsl::Overlay::get()->getCurrentGui()->removeFocus();
            rebuildUI();
            rootFrame->invalidate();
            tsl::Overlay::get()->getCurrentGui()->requestFocus(rootFrame, tsl::FocusDirection::None);
        }
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<GuiMain>();
    }
};

int main(int argc, char **argv) {
    return tsl::loop<OverlayTeslaMenu, tsl::impl::LaunchFlags::None>(argc, argv);
}
