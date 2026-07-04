## Make sure to place gptokeyb2 (arm64) binary in packaging/retro-handheld

RH_PACK_DIR=$(ROOTDIR)/packaging/retro-handheld
RH_PKG_DIR=pkgbuild
RH_ROCKBOX_DIR=$(RH_PKG_DIR)/rockbox

MUOS_PKG_DIR=$(RH_PKG_DIR)/muos
NEXTUI_PKG_DIR=$(RH_PKG_DIR)/nextui
PM_PKG_DIR=$(RH_PKG_DIR)/portmaster

rhbuild:
	mkdir $(RH_PKG_DIR)
	make PREFIX=$(RH_PKG_DIR)/build fullinstall
	mkdir $(RH_ROCKBOX_DIR)
	## Organize files
	mv $(RH_PKG_DIR)/build/bin/rockbox $(RH_ROCKBOX_DIR)
	mv $(RH_PKG_DIR)/build/lib/rockbox/* $(RH_ROCKBOX_DIR)
	mv $(RH_PKG_DIR)/build/share/rockbox/* $(RH_ROCKBOX_DIR)
	## Plugin workarounds
	mkdir $(RH_ROCKBOX_DIR)/rocks.data
	mv $(RH_ROCKBOX_DIR)/rocks/games/.picross $(RH_ROCKBOX_DIR)/rocks.data/.picross
	## Permissions
	chmod +x $(RH_ROCKBOX_DIR)/rockbox
	## Copy licenses over
	cp -R $(RH_PACK_DIR)/licenses $(RH_ROCKBOX_DIR)
	## Bundle Aurora / Aurora Light theme assets (wps, sbs, bitmaps incl. backdrops, theme cfg)
	## Obsede2 is intentionally not bundled -- Aurora/Aurora Light are the only
	## themes this pak ships. Both point their iconset: at the stock
	## tango_icons.16x16.bmp (already bundled by the normal fullinstall step,
	## same as cabbiev2 uses) -- no manual icon copy needed.
	mkdir -p $(RH_ROCKBOX_DIR)/wps/Aurora $(RH_ROCKBOX_DIR)/wps/AuroraLight $(RH_ROCKBOX_DIR)/themes
	cp $(ROOTDIR)/wps/Aurora.wps $(ROOTDIR)/wps/Aurora.sbs $(RH_ROCKBOX_DIR)/wps/
	cp $(ROOTDIR)/wps/AuroraLight.wps $(ROOTDIR)/wps/AuroraLight.sbs $(RH_ROCKBOX_DIR)/wps/
	cp -r $(ROOTDIR)/wps/Aurora/. $(RH_ROCKBOX_DIR)/wps/Aurora/
	cp -r $(ROOTDIR)/wps/AuroraLight/. $(RH_ROCKBOX_DIR)/wps/AuroraLight/
	cp $(ROOTDIR)/themes/Aurora.cfg $(ROOTDIR)/themes/AuroraLight.cfg $(RH_ROCKBOX_DIR)/themes/
	## Bundle prebuilt SF-Pro fonts (not .bdf sources, so convbdf cannot build them)
	mkdir -p $(RH_ROCKBOX_DIR)/fonts
	cp $(ROOTDIR)/fonts/*.fnt $(RH_ROCKBOX_DIR)/fonts/
	## Install default config (sets Aurora as the active theme on first boot)
	cp $(RH_PACK_DIR)/config.cfg $(RH_ROCKBOX_DIR)/config.cfg
	## Bundle Milkdrop visualizer presets (read at runtime from ROCKBOX_DIR/presets)
	mkdir -p $(RH_ROCKBOX_DIR)/presets
	cp $(ROOTDIR)/apps/milkdrop_presets/*.milk $(RH_ROCKBOX_DIR)/presets/
	cp $(ROOTDIR)/apps/milkdrop_presets/LICENSE.md $(RH_ROCKBOX_DIR)/presets/
	rm -rf $(RH_PKG_DIR)/build

## We no longer need muos specific build since portmaster build replaces it.
#
# muos: 
# 	mkdir -p $(MUOS_PKG_DIR)/Rockbox
# 	cp -R $(RH_ROCKBOX_DIR)/* $(MUOS_PKG_DIR)/Rockbox
# 	## Copy muOS specific files
# 	cp $(RH_PACK_DIR)/rockbox.gptk $(MUOS_PKG_DIR)/Rockbox
# 	cp $(RH_PACK_DIR)/mux_launch.sh $(MUOS_PKG_DIR)/Rockbox
# 	## Permissions
# 	chmod +x $(MUOS_PKG_DIR)/Rockbox/mux_launch.sh

# muosclean:
# 	rm -rf $(MUOS_PKG_DIR)

# muos-zip: muosclean muos
# 	(cd $(MUOS_PKG_DIR) && zip -q -r - .) >Rockbox.muxapp

nextui:
	mkdir -p $(NEXTUI_PKG_DIR)
	cp -R $(RH_ROCKBOX_DIR) $(NEXTUI_PKG_DIR)
	mv $(NEXTUI_PKG_DIR)/rockbox/licenses $(NEXTUI_PKG_DIR)
	## Copy NextUI specific files
	cp $(RH_PACK_DIR)/rockbox.gptk $(NEXTUI_PKG_DIR)
	cp $(RH_PACK_DIR)/gptokeyb2 $(NEXTUI_PKG_DIR)
	cp $(RH_PACK_DIR)/gamecontrollerdb.txt $(NEXTUI_PKG_DIR)
	cp $(RH_PACK_DIR)/pak.json $(NEXTUI_PKG_DIR)
	cp $(RH_PACK_DIR)/launch.sh $(NEXTUI_PKG_DIR)
	cp -R $(RH_PACK_DIR)/systems $(NEXTUI_PKG_DIR)/rockbox
	## Permissions
	chmod +x $(NEXTUI_PKG_DIR)/gptokeyb2
	chmod +x $(NEXTUI_PKG_DIR)/launch.sh

nextuiclean:
	rm -rf $(NEXTUI_PKG_DIR)

nextui-zip: nextuiclean nextui
	(cd $(NEXTUI_PKG_DIR) && zip -q -r - .) >Rockbox.pak.zip

portmaster:
	mkdir -p $(PM_PKG_DIR)
	cp -R $(RH_ROCKBOX_DIR) $(PM_PKG_DIR)
	## Copy Portmaster specific files
	cp $(RH_PACK_DIR)/gameinfo.xml $(PM_PKG_DIR)/rockbox
	cp $(RH_PACK_DIR)/rockbox.gptk $(PM_PKG_DIR)/rockbox
	cp $(RH_PACK_DIR)/mux_launch.txt $(PM_PKG_DIR)/rockbox
	cp $(RH_PACK_DIR)/screenshot.png $(PM_PKG_DIR)/rockbox
	cp $(RH_PACK_DIR)/port.json $(PM_PKG_DIR)/rockbox
	cp $(RH_PACK_DIR)/README.md $(PM_PKG_DIR)/rockbox
	cp -R $(RH_PACK_DIR)/firmware $(PM_PKG_DIR)/rockbox
	cp $(RH_PACK_DIR)/Rockbox.sh $(PM_PKG_DIR)
	rm -f $(PM_PKG_DIR)/rockbox/licenses/gptokeyb2.txt
	## Permissions
	chmod +x $(PM_PKG_DIR)/Rockbox.sh

portmasterclean:
	rm -rf $(PM_PKG_DIR)

portmaster-zip: portmasterclean portmaster
	(cd $(PM_PKG_DIR) && zip -q -r - .) >rockbox.zip

rhclean:
	rm -rf $(RH_PKG_DIR)

rhall-zip: rhclean rhbuild nextui-zip portmaster-zip
