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
	## Bundle Obsede2 theme assets (wps, sbs, bitmaps, theme cfg, iconset)
	mkdir -p $(RH_ROCKBOX_DIR)/wps/Obsede2 $(RH_ROCKBOX_DIR)/themes $(RH_ROCKBOX_DIR)/icons
	cp $(ROOTDIR)/wps/Obsede2.wps $(RH_ROCKBOX_DIR)/wps/
	cp $(ROOTDIR)/wps/Obsede2.sbs $(RH_ROCKBOX_DIR)/wps/
	cp -r $(ROOTDIR)/wps/Obsede2/. $(RH_ROCKBOX_DIR)/wps/Obsede2/
	cp $(ROOTDIR)/themes/Obsede2.cfg $(RH_ROCKBOX_DIR)/themes/
	cp $(ROOTDIR)/icons/icons_5px.bmp $(RH_ROCKBOX_DIR)/icons/
	## Bundle prebuilt SF-Pro fonts (not .bdf sources, so convbdf cannot build them)
	mkdir -p $(RH_ROCKBOX_DIR)/fonts
	cp $(ROOTDIR)/fonts/*.fnt $(RH_ROCKBOX_DIR)/fonts/
	## Install default config (sets Obsede2 as the active theme on first boot)
	cp $(RH_PACK_DIR)/config.cfg $(RH_ROCKBOX_DIR)/config.cfg
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
	rm $(PM_PKG_DIR)/rockbox/licenses/gptokeyb2.txt
	## Permissions
	chmod +x $(PM_PKG_DIR)/Rockbox.sh

portmasterclean:
	rm -rf $(PM_PKG_DIR)

portmaster-zip: portmasterclean portmaster
	(cd $(PM_PKG_DIR) && zip -q -r - .) >rockbox.zip

rhclean:
	rm -rf $(RH_PKG_DIR)

rhall-zip: rhclean rhbuild nextui-zip portmaster-zip
