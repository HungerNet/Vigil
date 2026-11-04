# Lunar Client — mappings Frenchify (vanilla obf → MCP)

> **Règle projet** : le code Frenchify de base (`src/`) n'est **pas modifié**.
> La build Lunar (`release_lunar/`) reprend la même logique et applique uniquement
> les mappings via `scripts/convert_lunar_mappings.py` + `McFindClass` / ClassLoader Lunar.

## Workflow

1. Copier `src/*.cpp` / `src/*.h` vers `release_lunar/src/` (garder `jni_mc_loader`, `injector`).
2. Exécuter `python scripts/convert_lunar_mappings.py` dans `release_lunar/`.
3. Bytecode Java : `java_src/FrenchifyTabOverlayNativeLunar.java` (noms MCP).
4. Headers : `java_src/lunar_headers/*Lunar.h`.

Sources : `mcp/joined.srg`, `mcp_csv/fields.csv`, `mcp_csv/methods.csv`.

---

## Classes JNI

| Obf vanilla | MCP Lunar | Fichiers |
|-------------|-----------|----------|
| `a` | `net/minecraft/util/EnumChatFormatting` | utils.cpp |
| `aug` | `net/minecraft/util/AxisAlignedBB` | colored_hitboxes.cpp |
| `aul` | `net/minecraft/scoreboard/ScorePlayerTeam` | dllmain.cpp, tab_overlay.cpp |
| `ave` | `net/minecraft/client/Minecraft` | dllmain.cpp, tab_overlay.cpp |
| `avn` | `net/minecraft/client/gui/FontRenderer` | tab_overlay.cpp |
| `avo` | `net/minecraft/client/gui/GuiIngame` | tab_overlay.cpp |
| `avp` | `net/minecraft/client/gui/Gui` | tab_overlay.cpp |
| `avr` | `net/minecraft/client/gui/ScaledResolution` | tab_overlay.cpp |
| `awh` | `net/minecraft/client/gui/GuiPlayerTabOverlay` | tab_overlay.cpp |
| `awv` | `net/minecraft/client/gui/GuiChat` | dllmain.cpp |
| `ayl` | `net/minecraft/client/gui/inventory/GuiContainer` | dllmain.cpp |
| `ayr` | `net/minecraft/client/gui/inventory/GuiChest` | dllmain.cpp |
| `bcy` | `net/minecraft/client/network/NetHandlerPlayClient` | tab_overlay.cpp |
| `bdc` | `net/minecraft/client/network/NetworkPlayerInfo` | tab_overlay.cpp |
| `bet` | `net/minecraft/client/entity/AbstractClientPlayer` | colored_hitboxes.cpp, denick.cpp, utils.cpp |
| `bew` | `net/minecraft/client/entity/EntityPlayerSP` | tab_overlay.cpp |
| `bex` | `net/minecraft/client/entity/EntityOtherPlayerMP` | colored_hitboxes.cpp |
| `biu` | `net/minecraft/client/renderer/entity/RenderManager` | colored_hitboxes.cpp |
| `bmj` | `net/minecraft/client/renderer/texture/TextureManager` | tab_overlay.cpp |
| `bnp` | `net/minecraft/client/resources/SkinManager` | denick.cpp |
| `bpf` | `net/minecraft/client/audio/PositionedSoundRecord` | jni_helpers.cpp |
| `eu` | `net/minecraft/util/IChatComponent` | tab_overlay.cpp, utils.cpp |
| `eu$a` | `net/minecraft/util/IChatComponent$Serializer` | dllmain.cpp, jni_helpers.cpp |
| `ez` | `net/minecraft/util/ChatStyle` | utils.cpp |
| `fa` | `net/minecraft/util/ChatComponentText` | jni_helpers.cpp, utils.cpp |
| `jy` | `net/minecraft/util/ResourceLocation` | jni_helpers.cpp |
| `pk` | `net/minecraft/entity/Entity` | anticheat.cpp, colored_hitboxes.cpp, utils.cpp |
| `pr` | `net/minecraft/entity/EntityLivingBase` | anticheat.cpp, utils.cpp |
| `pz` | `net/minecraft/entity/DataWatcher` | anticheat.cpp |
| `wn` | `net/minecraft/entity/player/EntityPlayer` | denick.cpp |

---

## Champs

| Classe MCP | Obf | MCP | JNI sig | Fichiers |
|------------|-----|-----|---------|----------|
| `?` | `P` | `P` | `D` | colored_hitboxes.cpp |
| `?` | `Q` | `Q` | `D` | colored_hitboxes.cpp |
| `?` | `R` | `R` | `D` | colored_hitboxes.cpp |
| `?` | `R` | `R` | `Lnet/minecraft/client/renderer/texture/TextureManager;` | tab_overlay.cpp |
| `?` | `a` | `a` | `D` | colored_hitboxes.cpp |
| `?` | `api_aurora` | `api_aurora` | `Ljava/lang/String;` | dllmain.cpp |
| `?` | `api_hypixel` | `api_hypixel` | `Ljava/lang/String;` | dllmain.cpp |
| `?` | `api_seraph` | `api_seraph` | `Ljava/lang/String;` | dllmain.cpp |
| `?` | `api_winstreak` | `api_winstreak` | `Ljava/lang/String;` | dllmain.cpp |
| `?` | `autoGL` | `autoGL` | `Z` | dllmain.cpp |
| `?` | `autoWho` | `autoWho` | `Z` | dllmain.cpp |
| `?` | `b` | `b` | `D` | colored_hitboxes.cpp |
| `?` | `c` | `c` | `F` | colored_hitboxes.cpp |
| `?` | `c` | `c` | `I` | colored_hitboxes.cpp |
| `?` | `c` | `c` | `D` | colored_hitboxes.cpp |
| `?` | `d` | `d` | `D` | colored_hitboxes.cpp |
| `?` | `d` | `d` | `I` | dllmain.cpp, tab_overlay.cpp |
| `?` | `e` | `e` | `F` | colored_hitboxes.cpp |
| `?` | `e` | `e` | `D` | colored_hitboxes.cpp |
| `?` | `e` | `e` | `I` | dllmain.cpp, tab_overlay.cpp |
| `?` | `enabled` | `enabled` | `Z` | dllmain.cpp |
| `?` | `f` | `f` | `F` | colored_hitboxes.cpp |
| `?` | `f` | `f` | `D` | colored_hitboxes.cpp |
| `?` | `fastBuy` | `fastBuy` | `Z` | dllmain.cpp |
| `?` | `gl_message` | `gl_message` | `Ljava/lang/String;` | dllmain.cpp |
| `?` | `h` | `h` | `Ljava/util/List;` | dllmain.cpp, jni_helpers.cpp |
| `?` | `h` | `h` | `Lnet/minecraft/client/entity/EntityPlayerSP;` | tab_overlay.cpp |
| `?` | `h` | `h` | `Lnet/minecraft/util/IChatComponent;` | tab_overlay.cpp |
| `?` | `i` | `i` | `Ljava/util/List;` | jni_helpers.cpp |
| `?` | `k` | `k` | `Lnet/minecraft/client/gui/FontRenderer;` | tab_overlay.cpp |
| `?` | `l` | `l` | `F` | colored_hitboxes.cpp |
| `?` | `m` | `m` | `F` | colored_hitboxes.cpp |
| `?` | `overlayGamemodeActive` | `overlayGamemodeActive` | `Ljava/lang/String;` | dllmain.cpp, jni_helpers.cpp |
| `?` | `overlayGamemodeEdit` | `overlayGamemodeEdit` | `Ljava/lang/String;` | dllmain.cpp, jni_helpers.cpp |
| `?` | `overlayOpacity` | `overlayOpacity` | `I` | dllmain.cpp, jni_helpers.cpp |
| `?` | `overlayResourceHeader` | `overlayResourceHeader` | `Z` | dllmain.cpp |
| `?` | `partyDetector` | `partyDetector` | `Z` | dllmain.cpp |
| `?` | `q` | `q` | `Lnet/minecraft/client/gui/GuiIngame;` | tab_overlay.cpp |
| `?` | `s` | `s` | `D` | colored_hitboxes.cpp |
| `?` | `t` | `t` | `D` | colored_hitboxes.cpp |
| `?` | `tabOverlay` | `tabOverlay` | `Z` | dllmain.cpp |
| `?` | `u` | `u` | `D` | colored_hitboxes.cpp |
| `?` | `v` | `v` | `Lnet/minecraft/client/gui/GuiPlayerTabOverlay;` | dllmain.cpp, tab_overlay.cpp |
| `?` | `y` | `y` | `F` | colored_hitboxes.cpp |
| `?` | `z` | `z` | `F` | colored_hitboxes.cpp |
| `net/minecraft/client/Minecraft` | `m` | `currentScreen` | `Lnet/minecraft/client/gui/GuiScreen;` | dllmain.cpp |
| `net/minecraft/client/Minecraft` | `e` | `displayHeight` | `I` | colored_hitboxes.cpp |
| `net/minecraft/client/Minecraft` | `d` | `displayWidth` | `I` | colored_hitboxes.cpp |
| `net/minecraft/client/Minecraft` | `q` | `ingameGUI` | `Lnet/minecraft/client/gui/GuiIngame;` | dllmain.cpp, jni_helpers.cpp |
| `net/minecraft/client/Minecraft` | `c` | `playerController` | `Lnet/minecraft/client/multiplayer/PlayerControllerMP;` | dllmain.cpp |
| `net/minecraft/client/Minecraft` | `g` | `renderGlobal` | `Lnet/minecraft/client/renderer/RenderGlobal;` | colored_hitboxes.cpp |
| `net/minecraft/client/Minecraft` | `h` | `thePlayer` | `Lnet/minecraft/client/entity/EntityPlayerSP;` | anticheat.cpp, colored_hitboxes.cpp, dllmain.cpp, utils.cpp |
| `net/minecraft/client/Minecraft` | `f` | `theWorld` | `Lnet/minecraft/client/multiplayer/WorldClient;` | anticheat.cpp, denick.cpp, utils.cpp |
| `net/minecraft/client/Minecraft` | `Y` | `timer` | `Lnet/minecraft/util/Timer;` | colored_hitboxes.cpp |
| `net/minecraft/client/entity/EntityPlayerSP` | `c` | `mc` | `I` | anticheat.cpp |
| `net/minecraft/client/entity/EntityPlayerSP` | `a` | `sendQueue` | `Lnet/minecraft/client/network/NetHandlerPlayClient;` | tab_overlay.cpp |
| `net/minecraft/client/gui/GuiChat` | `a` | `inputField` | `Lnet/minecraft/client/gui/GuiTextField;` | dllmain.cpp |
| `net/minecraft/client/gui/inventory/GuiChest` | `w` | `lowerChestInventory` | `Lnet/minecraft/inventory/IInventory;` | dllmain.cpp |
| `net/minecraft/client/gui/inventory/GuiContainer` | `h` | `inventorySlots` | `Lnet/minecraft/inventory/Container;` | dllmain.cpp |
| `net/minecraft/client/gui/inventory/GuiContainer` | `u` | `theSlot` | `Lnet/minecraft/inventory/Slot;` | dllmain.cpp |
| `net/minecraft/entity/Entity` | `ae` | `chunkCoordX` | `Lnet/minecraft/entity/Entity;` | anticheat.cpp |
| `net/minecraft/entity/Entity` | `c` | `entityId` | `I` | anticheat.cpp, colored_hitboxes.cpp |
| `net/minecraft/entity/Entity` | `ar` | `entityRiderPitchDelta` | `Z` | anticheat.cpp |
| `net/minecraft/entity/Entity` | `as` | `entityRiderYawDelta` | `I` | anticheat.cpp |
| `net/minecraft/entity/Entity` | `P` | `lastTickPosX` | `D` | colored_hitboxes.cpp |
| `net/minecraft/entity/Entity` | `Q` | `lastTickPosY` | `D` | colored_hitboxes.cpp |
| `net/minecraft/entity/Entity` | `R` | `lastTickPosZ` | `D` | colored_hitboxes.cpp |
| `net/minecraft/entity/Entity` | `s` | `posX` | `D` | anticheat.cpp, colored_hitboxes.cpp, utils.cpp |
| `net/minecraft/entity/Entity` | `t` | `posY` | `D` | anticheat.cpp, colored_hitboxes.cpp, utils.cpp |
| `net/minecraft/entity/Entity` | `u` | `posZ` | `D` | anticheat.cpp, colored_hitboxes.cpp, utils.cpp |
| `net/minecraft/entity/Entity` | `l` | `riddenByEntity` | `F` | colored_hitboxes.cpp |
| `net/minecraft/entity/Entity` | `m` | `ridingEntity` | `F` | colored_hitboxes.cpp |
| `net/minecraft/entity/Entity` | `z` | `rotationPitch` | `F` | anticheat.cpp, colored_hitboxes.cpp |
| `net/minecraft/entity/Entity` | `y` | `rotationYaw` | `F` | colored_hitboxes.cpp |
| `net/minecraft/world/World` | `f` | `loadedEntityList` | `Ljava/util/List;` | colored_hitboxes.cpp |
| `net/minecraft/world/World` | `j` | `playerEntities` | `Ljava/util/List;` | anticheat.cpp, colored_hitboxes.cpp, denick.cpp, utils.cpp |

---

## Méthodes

| Classe MCP | Obf | MCP | JNI sig | Fichiers |
|------------|-----|-----|---------|----------|
| `?` | `a` | `a` | `(Ljava/lang/String;)V` | dllmain.cpp |
| `?` | `a` | `a` | `()Lnet/minecraft/util/IChatComponent;` | dllmain.cpp |
| `?` | `a` | `a` | `(I)Lnet/minecraft/item/ItemStack;` | dllmain.cpp |
| `?` | `a` | `a` | `(Lnet/minecraft/util/IChatComponent;)V` | jni_helpers.cpp |
| `?` | `a` | `a` | `()I` | tab_overlay.cpp |
| `?` | `a` | `a` | `(Ljava/lang/String;)I` | tab_overlay.cpp |
| `?` | `a` | `a` | `(Lnet/minecraft/util/ResourceLocation;)V` | tab_overlay.cpp |
| `?` | `a` | `a` | `()Lcom/mojang/authlib/GameProfile;` | tab_overlay.cpp |
| `?` | `b` | `b` | `(Lnet/minecraft/entity/Entity;DDDFF)V` | colored_hitboxes.cpp |
| `?` | `b` | `b` | `()Ljava/lang/String;` | dllmain.cpp |
| `?` | `b` | `b` | `()I` | dllmain.cpp, tab_overlay.cpp |
| `?` | `c` | `c` | `()I` | dllmain.cpp, tab_overlay.cpp |
| `?` | `c` | `c` | `()Ljava/lang/String;` | tab_overlay.cpp |
| `?` | `d` | `d` | `()Ljava/lang/String;` | dllmain.cpp, tab_overlay.cpp, utils.cpp |
| `?` | `d` | `d` | `()Ljava/util/Collection;` | tab_overlay.cpp |
| `?` | `a` | `drawModalRectWithCustomSizedTexture` | `(IIFFIIFF)V` | tab_overlay.cpp |
| `?` | `a` | `drawOutlinedBoundingBox` | `(Lnet/minecraft/util/AxisAlignedBB;IIII)V` | colored_hitboxes.cpp |
| `?` | `a` | `drawStringWithShadow` | `(Ljava/lang/String;FFI)I` | tab_overlay.cpp |
| `?` | `e` | `e` | `(Ljava/lang/String;)V` | dllmain.cpp |
| `?` | `e` | `e` | `()Ljava/lang/String;` | dllmain.cpp, tab_overlay.cpp |
| `?` | `entries` | `entries` | `()Ljava/util/Set;` | denick.cpp |
| `?` | `a` | `formatPlayerName` | `(Lnet/minecraft/scoreboard/Team;Ljava/lang/String;)Ljava/lang/String;` | tab_overlay.cpp |
| `?` | `get` | `get` | `(I)Ljava/lang/Object;` | anticheat.cpp, colored_hitboxes.cpp, denick.cpp, dllmain.cpp, utils.cpp |
| `?` | `get` | `get` | `(Ljava/lang/String;)Ljava/util/Collection;` | denick.cpp |
| `?` | `get` | `get` | `(Ljava/lang/Object;)Ljava/lang/Object;` | denick.cpp |
| `?` | `d` | `getChatGUI` | `()Lnet/minecraft/client/gui/GuiNewChat;` | dllmain.cpp, jni_helpers.cpp |
| `?` | `getClass` | `getClass` | `()Ljava/lang/Class;` | colored_hitboxes.cpp, dllmain.cpp |
| `?` | `getClassLoader` | `getClassLoader` | `()Ljava/lang/ClassLoader;` | colored_hitboxes.cpp, dllmain.cpp |
| `?` | `S` | `getCollisionBoundingBox` | `()Lnet/minecraft/util/AxisAlignedBB;` | colored_hitboxes.cpp |
| `?` | `aR` | `getEntityBoundingBox` | `()Lnet/minecraft/util/AxisAlignedBB;` | colored_hitboxes.cpp |
| `?` | `cd` | `getGameProfile` | `()Lcom/mojang/authlib/GameProfile;` | tab_overlay.cpp |
| `?` | `getId` | `getId` | `()Ljava/util/UUID;` | anticheat.cpp, tab_overlay.cpp, utils.cpp |
| `?` | `getKey` | `getKey` | `()Ljava/lang/Object;` | denick.cpp |
| `?` | `g` | `getLocationSkin` | `()Lnet/minecraft/util/ResourceLocation;` | tab_overlay.cpp |
| `?` | `A` | `getMinecraft` | `()Lnet/minecraft/client/Minecraft;` | tab_overlay.cpp |
| `?` | `getName` | `getName` | `()Ljava/lang/String;` | anticheat.cpp, denick.cpp, tab_overlay.cpp, utils.cpp |
| `?` | `a` | `getObjectiveInDisplaySlot` | `(I)Lnet/minecraft/scoreboard/ScoreObjective;` | dllmain.cpp, tab_overlay.cpp |
| `?` | `getOverlayColumnsConfig` | `getOverlayColumnsConfig` | `()Ljava/lang/String;` | dllmain.cpp |
| `?` | `h` | `getPlayersTeam` | `(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScorePlayerTeam;` | dllmain.cpp, tab_overlay.cpp, utils.cpp |
| `?` | `i` | `getSortedScores` | `(Lnet/minecraft/scoreboard/ScoreObjective;)Ljava/util/Collection;` | dllmain.cpp, tab_overlay.cpp |
| `?` | `getValue` | `getValue` | `()Ljava/lang/String;` | denick.cpp |
| `?` | `getValue` | `getValue` | `()Ljava/lang/Object;` | denick.cpp |
| `?` | `cp` | `getWorldScoreboard` | `()Lnet/minecraft/scoreboard/Scoreboard;` | dllmain.cpp, tab_overlay.cpp |
| `?` | `hasNext` | `hasNext` | `()Z` | anticheat.cpp, denick.cpp, dllmain.cpp, tab_overlay.cpp |
| `?` | `iterator` | `iterator` | `()Ljava/util/Iterator;` | anticheat.cpp, denick.cpp, dllmain.cpp, tab_overlay.cpp |
| `?` | `loadClass` | `loadClass` | `(Ljava/lang/String;)Ljava/lang/Class;` | colored_hitboxes.cpp, dllmain.cpp |
| `?` | `loadOverlayColumnsForEditMode` | `loadOverlayColumnsForEditMode` | `()V` | dllmain.cpp |
| `?` | `next` | `next` | `()Ljava/lang/Object;` | anticheat.cpp, denick.cpp, dllmain.cpp, tab_overlay.cpp |
| `?` | `a` | `playSound` | `(Lnet/minecraft/client/audio/ISound;)V` | jni_helpers.cpp |
| `?` | `remove` | `remove` | `(I)Ljava/lang/Object;` | jni_helpers.cpp |
| `?` | `setOverlayGamemodeActive` | `setOverlayGamemodeActive` | `(Ljava/lang/String;)V` | dllmain.cpp |
| `?` | `size` | `size` | `()I` | anticheat.cpp, colored_hitboxes.cpp, denick.cpp, dllmain.cpp, jni_helpers.cpp, utils.cpp |
| `?` | `snapshotOverlayColumnsEditMode` | `snapshotOverlayColumnsEditMode` | `()V` | jni_helpers.cpp |
| `?` | `toString` | `toString` | `()Ljava/lang/String;` | anticheat.cpp, tab_overlay.cpp |
| `?` | `values` | `values` | `()Ljava/util/Collection;` | denick.cpp |
| `?` | `a` | `windowClick` | `(IIIILnet/minecraft/entity/player/EntityPlayer;)Lnet/minecraft/item/ItemStack;` | dllmain.cpp |
| `net/minecraft/client/Minecraft` | `a` | `addScheduledTask` | `(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;` | colored_hitboxes.cpp |
| `net/minecraft/client/Minecraft` | `a` | `displayGuiScreen` | `(Lnet/minecraft/client/gui/GuiScreen;)V` | dllmain.cpp |
| `net/minecraft/client/Minecraft` | `A` | `getMinecraft` | `()Lnet/minecraft/client/Minecraft;` | dllmain.cpp |
| `net/minecraft/client/Minecraft` | `u` | `getNetHandler` | `()Lnet/minecraft/client/network/NetHandlerPlayClient;` | anticheat.cpp, utils.cpp |
| `net/minecraft/client/Minecraft` | `af` | `getRenderManager` | `()Lnet/minecraft/client/renderer/entity/RenderManager;` | colored_hitboxes.cpp |
| `net/minecraft/client/Minecraft` | `ab` | `getSkinManager` | `()Lnet/minecraft/client/resources/SkinManager;` | denick.cpp |
| `net/minecraft/client/Minecraft` | `W` | `getSoundHandler` | `()Lnet/minecraft/client/audio/SoundHandler;` | jni_helpers.cpp |
| `net/minecraft/client/audio/PositionedSoundRecord` | `a` | `create` | `(Lnet/minecraft/util/ResourceLocation;F)Lnet/minecraft/client/audio/PositionedSoundRecord;` | jni_helpers.cpp |
| `net/minecraft/client/audio/PositionedSoundRecord` | `a` | `create` | `(Lnet/minecraft/util/ResourceLocation;)Lnet/minecraft/client/audio/PositionedSoundRecord;` | jni_helpers.cpp |
| `net/minecraft/client/entity/AbstractClientPlayer` | `i` | `getLocationSkin` | `()Lnet/minecraft/util/ResourceLocation;` | denick.cpp |
| `net/minecraft/client/entity/EntityPlayerSP` | `bS` | `bS` | `()Z` | utils.cpp |
| `net/minecraft/client/network/NetHandlerPlayClient` | `a` | `getPlayerInfo` | `(Ljava/util/UUID;)Lnet/minecraft/client/network/NetworkPlayerInfo;` | anticheat.cpp, utils.cpp |
| `net/minecraft/client/network/NetHandlerPlayClient` | `d` | `getPlayerInfoMap` | `()Ljava/util/Collection;` | anticheat.cpp |
| `net/minecraft/client/network/NetworkPlayerInfo` | `k` | `getDisplayName` | `()Lnet/minecraft/util/IChatComponent;` | anticheat.cpp |
| `net/minecraft/client/network/NetworkPlayerInfo` | `a` | `getGameProfile` | `()Lcom/mojang/authlib/GameProfile;` | anticheat.cpp |
| `net/minecraft/client/network/NetworkPlayerInfo` | `i` | `getPlayerTeam` | `()Lnet/minecraft/scoreboard/ScorePlayerTeam;` | anticheat.cpp, utils.cpp |
| `net/minecraft/client/network/NetworkPlayerInfo` | `i` | `i` | `()Lnet/minecraft/scoreboard/Team;` | utils.cpp |
| `net/minecraft/entity/DataWatcher` | `d` | `getWatchableObjectFloat` | `(I)F` | anticheat.cpp |
| `net/minecraft/entity/Entity` | `bS` | `bS` | `()Z` | anticheat.cpp |
| `net/minecraft/entity/Entity` | `bW` | `bW` | `()Z` | anticheat.cpp |
| `net/minecraft/entity/Entity` | `S` | `getCollisionBoundingBox` | `()Lnet/minecraft/util/AxisAlignedBB;` | colored_hitboxes.cpp |
| `net/minecraft/entity/Entity` | `bZ` | `getCurrentEquippedItem` | `()Lnet/minecraft/item/ItemStack;` | anticheat.cpp, utils.cpp |
| `net/minecraft/entity/Entity` | `H` | `getDataWatcher` | `()Lnet/minecraft/entity/DataWatcher;` | anticheat.cpp |
| `net/minecraft/entity/Entity` | `f_` | `getDisplayName` | `()Lnet/minecraft/util/IChatComponent;` | utils.cpp |
| `net/minecraft/entity/Entity` | `aR` | `getEntityBoundingBox` | `()Lnet/minecraft/util/AxisAlignedBB;` | colored_hitboxes.cpp |
| `net/minecraft/entity/Entity` | `cd` | `getGameProfile` | `()Lcom/mojang/authlib/GameProfile;` | anticheat.cpp, utils.cpp |
| `net/minecraft/entity/Entity` | `bn` | `getHealth` | `()F` | anticheat.cpp |
| `net/minecraft/entity/Entity` | `bA` | `getHeldItem` | `()Lnet/minecraft/item/ItemStack;` | anticheat.cpp, utils.cpp |
| `net/minecraft/entity/Entity` | `e_` | `getName` | `()Ljava/lang/String;` | utils.cpp |
| `net/minecraft/entity/Entity` | `b` | `getPlayerInfo` | `()Lnet/minecraft/client/network/NetworkPlayerInfo;` | utils.cpp |
| `net/minecraft/entity/Entity` | `bO` | `getTeam` | `()Lnet/minecraft/scoreboard/Team;` | utils.cpp |
| `net/minecraft/entity/Entity` | `ax` | `isInvisible` | `()Z` | colored_hitboxes.cpp |
| `net/minecraft/entity/Entity` | `av` | `isSneaking` | `()Z` | anticheat.cpp |
| `net/minecraft/entity/Entity` | `aw` | `isSprinting` | `()Z` | anticheat.cpp |
| `net/minecraft/entity/EntityLivingBase` | `bn` | `getHealth` | `()F` | anticheat.cpp |
| `net/minecraft/entity/EntityLivingBase` | `bO` | `getTeam` | `()Lnet/minecraft/scoreboard/Team;` | utils.cpp |
| `net/minecraft/entity/player/EntityPlayer` | `cd` | `getGameProfile` | `()Lcom/mojang/authlib/GameProfile;` | denick.cpp |
| `net/minecraft/item/Item` | `b` | `getIdFromItem` | `(Lnet/minecraft/item/Item;)I` | anticheat.cpp, utils.cpp |
| `net/minecraft/item/ItemStack` | `q` | `getDisplayName` | `()Ljava/lang/String;` | dllmain.cpp |
| `net/minecraft/item/ItemStack` | `b` | `getItem` | `()Lnet/minecraft/item/Item;` | anticheat.cpp, utils.cpp |
| `net/minecraft/scoreboard/ScorePlayerTeam` | `a` | `formatPlayerName` | `(Lnet/minecraft/scoreboard/Team;Ljava/lang/String;)Ljava/lang/String;` | dllmain.cpp |
| `net/minecraft/scoreboard/ScorePlayerTeam` | `l` | `getChatFormat` | `()Lnet/minecraft/util/EnumChatFormatting;` | utils.cpp |
| `net/minecraft/scoreboard/ScorePlayerTeam` | `d` | `getMembershipCollection` | `()Ljava/util/Collection;` | anticheat.cpp |
| `net/minecraft/scoreboard/ScorePlayerTeam` | `b` | `getRegisteredName` | `()Ljava/lang/String;` | utils.cpp |
| `net/minecraft/util/ChatComponentText` | `a` | `setChatStyle` | `(Lnet/minecraft/util/ChatStyle;)Lnet/minecraft/util/IChatComponent;` | utils.cpp |
| `net/minecraft/util/ChatStyle` | `a` | `setColor` | `(Lnet/minecraft/util/EnumChatFormatting;)Lnet/minecraft/util/ChatStyle;` | utils.cpp |
| `net/minecraft/util/EnumChatFormatting` | `b` | `getColorIndex` | `()I` | utils.cpp |
| `net/minecraft/util/IChatComponent` | `a` | `appendSibling` | `(Lnet/minecraft/util/IChatComponent;)Lnet/minecraft/util/IChatComponent;` | utils.cpp |
| `net/minecraft/util/IChatComponent` | `f` | `createCopy` | `()Lnet/minecraft/util/IChatComponent;` | utils.cpp |
| `net/minecraft/util/IChatComponent` | `d` | `getFormattedText` | `()Ljava/lang/String;` | utils.cpp |
| `net/minecraft/util/IChatComponent` | `a` | `getSiblings` | `()Ljava/util/List;` | utils.cpp |
| `net/minecraft/util/IChatComponent` | `c` | `getUnformattedText` | `()Ljava/lang/String;` | anticheat.cpp, utils.cpp |
| `net/minecraft/util/IChatComponent$Serializer` | `a` | `componentToJson` | `(Lnet/minecraft/util/IChatComponent;)Ljava/lang/String;` | dllmain.cpp |
| `net/minecraft/util/IChatComponent$Serializer` | `a` | `jsonToComponent` | `(Ljava/lang/String;)Lnet/minecraft/util/IChatComponent;` | jni_helpers.cpp |
| `net/minecraft/world/World` | `Z` | `getScoreboard` | `()Lnet/minecraft/scoreboard/Scoreboard;` | utils.cpp |

---

## Bytecode Java injecté

| Vanilla | Lunar |
|---------|-------|
| `extends awh` | `extends GuiPlayerTabOverlay` |
| `ave` / `avo` | `Minecraft` / `GuiIngame` |
| `a(int, auo, auk)` | `renderPlayerlist(int, Scoreboard, ScoreObjective)` |

---

## Anticheat (référence rapide)

| Obf | MCP | Classe |
|-----|-----|--------|
| `c` | `entityId` | `Entity` |
| `z` | `rotationPitch` | `Entity` |
| `s`/`t`/`u` | `posX`/`posY`/`posZ` | `Entity` |
| `ae` | `ridingEntity` | `Entity` |
| `ar`/`as` | `isSwingInProgress`/`swingProgressInt` | `EntityLivingBase` |
| `bW`/`bS` | `isBlocking`/`isUsingItem` | `EntityPlayer` |
| `bZ`/`bA` | `getHeldItem` | `EntityLivingBase` |
| `au`/`av`/`aO`/`aw` | `hurtTime`/`maxHurtTime`/`recentlyHit`/`attackedAtYaw` | `EntityLivingBase` |
| `Z` | `hurtResistantTime` | `Entity` |
| `bn` | `getHealth` | `EntityLivingBase` |
| `H` | `getDataWatcher` | `Entity` |
| `d` | `getWatchableObjectFloat` | `DataWatcher` |

---

## Ressources

- [Lunar Mapping Project](https://github.com/Lunar-Mapping-Project/mappings)
- [MCP Thiakil 1.8.9](https://mcp.thiakil.com)

