# Frenchify Inject — Classes & obfuscation (Minecraft 1.8.9)

Référence des classes Java utilisées par l'inject C++/JNI (`src/`) avec leurs noms **obfusqués vanilla** et leurs noms **MCP** (lisibles).

> **Lunar Client** : ce fichier couvre le launcher vanilla uniquement. Pour les noms obfusqués Lunar (`com/moonsworth/lunar/…`) et ce qu’il faut passer à `McFindClass`, voir [`LUNAR_CLIENT_OBF.md`](LUNAR_CLIENT_OBF.md).

---

## Légende

| Colonne | Description |
|---------|-------------|
| **Obf** | Nom interne vanilla (`FindClass("ave")`, signatures JNI `Lave;`, etc.) |
| **MCP** | Nom déobfusqué (`net/minecraft/client/Minecraft`) |
| **Fichiers** | Fichiers C++ / Java du projet qui y font référence |

---

## Classes injectées Frenchify (non obfusquées)

Ces classes sont compilées puis injectées via `DefineClass` / `loadClass` dans le classloader du jeu.

| Classe | Étend / implémente | Rôle | Fichiers |
|--------|-------------------|------|----------|
| `FrenchifyGui4` | `axu` (`GuiScreen`) | GUI `/config` | `dllmain.cpp`, `java_src/FrenchifyGui4.java`, `FrenchifyGui.h` |
| `FrenchifyTabOverlayNative` | `awh` (`GuiPlayerTabOverlay`) | Tab overlay custom (hook render) | `dllmain.cpp`, `java_src/FrenchifyTabOverlayNative.java` |
| `FrenchifyTabOverlay` | `awh` | Variante tab (bytecode legacy) | `java_src/FrenchifyTabOverlay.java` |
| `FrenchifyRenderGlobal` | `bfr` (`RenderGlobal`) | Hook `renderEntities` → hitboxes | `colored_hitboxes.cpp`, `java_src/FrenchifyRenderGlobal.java` |
| `FrenchifyHitboxInstaller` | `Runnable` | Install du hook sur le client thread | `colored_hitboxes.cpp`, `java_src/FrenchifyHitboxInstaller.java` |
| `FrenchifyClientPump` | `Runnable` | Pump JNI côté client thread (Lunar) | `java_src/FrenchifyClientPumpLunar.java` |

---

## Client Minecraft

| Obf | MCP | Fichiers |
|-----|-----|----------|
| `ave` | `net/minecraft/client/Minecraft` | `dllmain.cpp`, `tab_overlay.cpp`, `anticheat.cpp`, `utils.cpp`, `denick.cpp`, `colored_hitboxes.cpp`, `jni_helpers.cpp` |
| `avo` | `net/minecraft/client/gui/GuiIngame` | `tab_overlay.cpp`, `dllmain.cpp`, `jni_helpers.cpp` |
| `avp` | `net/minecraft/client/gui/Gui` | `tab_overlay.cpp` |
| `avr` | `net/minecraft/client/gui/ScaledResolution` | `tab_overlay.cpp` |
| `avn` | `net/minecraft/client/gui/FontRenderer` | `tab_overlay.cpp` |
| `awh` | `net/minecraft/client/gui/GuiPlayerTabOverlay` | `tab_overlay.cpp`, `dllmain.cpp`, `FrenchifyTabOverlayNative.java` |
| `awv` | `net/minecraft/client/gui/GuiChat` | `dllmain.cpp` |
| `axu` | `net/minecraft/client/gui/GuiScreen` | `dllmain.cpp` (champ `currentScreen`) |
| `avw` | `net/minecraft/client/gui/GuiTextField` | `dllmain.cpp` (champ `inputField` de `GuiChat`) |
| `avt` | `net/minecraft/client/gui/GuiNewChat` | `dllmain.cpp`, `jni_helpers.cpp` (`getChatGUI`) |
| `ava` | `net/minecraft/client/gui/ChatLine` | `dllmain.cpp` (lignes chat, via `GetObjectClass`) |
| `ayl` | `net/minecraft/client/gui/inventory/GuiContainer` | `dllmain.cpp` (fast buy) |
| `ayr` | `net/minecraft/client/gui/inventory/GuiChest` | `dllmain.cpp` (fast buy) |
| `avl` | `net/minecraft/util/Timer` | `colored_hitboxes.cpp` |
| `bfr` | `net/minecraft/client/renderer/RenderGlobal` | `colored_hitboxes.cpp`, `FrenchifyRenderGlobal.java` |
| `biu` | `net/minecraft/client/renderer/entity/RenderManager` | `colored_hitboxes.cpp` |
| `bmj` | `net/minecraft/client/renderer/texture/TextureManager` | `tab_overlay.cpp` |
| `bnp` | `net/minecraft/client/resources/SkinManager` | `denick.cpp` |
| `bcy` | `net/minecraft/client/network/NetHandlerPlayClient` | `tab_overlay.cpp`, `anticheat.cpp`, `utils.cpp` |
| `bdc` | `net/minecraft/client/network/NetworkPlayerInfo` | `tab_overlay.cpp`, `anticheat.cpp`, `utils.cpp` |
| `bda` | `net/minecraft/client/multiplayer/PlayerControllerMP` | `dllmain.cpp` |
| `bdb` | `net/minecraft/client/multiplayer/WorldClient` | `anticheat.cpp`, `utils.cpp`, `denick.cpp`, `colored_hitboxes.cpp` |
| `adm` | `net/minecraft/world/World` | `colored_hitboxes.cpp` (fallback sig. `Lbdb;` / `Ladm;`) |
| `bet` | `net/minecraft/client/entity/AbstractClientPlayer` | `colored_hitboxes.cpp`, `utils.cpp`, `denick.cpp` |
| `bew` | `net/minecraft/client/entity/EntityPlayerSP` | `tab_overlay.cpp`, `dllmain.cpp`, `anticheat.cpp`, `utils.cpp`, `colored_hitboxes.cpp` |
| `bex` | `net/minecraft/client/entity/EntityOtherPlayerMP` | `colored_hitboxes.cpp` |
| `bpf` | `net/minecraft/client/audio/PositionedSoundRecord` | `jni_helpers.cpp` |
| `bpj` | `net/minecraft/client/audio/ISound` | `jni_helpers.cpp` (`playSound`) |
| `bpz` | `net/minecraft/client/audio/SoundHandler` | `jni_helpers.cpp` |
| `bia` | `net/minecraft/client/renderer/culling/ICamera` | `FrenchifyRenderGlobal.java` (param `renderEntities`) |

---

## Entités & monde

| Obf | MCP | Fichiers |
|-----|-----|----------|
| `pk` | `net/minecraft/entity/Entity` | `anticheat.cpp`, `colored_hitboxes.cpp`, `utils.cpp` |
| `pr` | `net/minecraft/entity/EntityLivingBase` | `anticheat.cpp`, `utils.cpp` |
| `pz` | `net/minecraft/entity/DataWatcher` | `anticheat.cpp` |
| `wn` | `net/minecraft/entity/player/EntityPlayer` | `denick.cpp`, `dllmain.cpp` (`windowClick`) |
| `aug` | `net/minecraft/util/AxisAlignedBB` | `colored_hitboxes.cpp` |

---

## Scoreboard

| Obf | MCP | Fichiers |
|-----|-----|----------|
| `auo` | `net/minecraft/scoreboard/Scoreboard` | `tab_overlay.cpp`, `dllmain.cpp`, `utils.cpp` |
| `auk` | `net/minecraft/scoreboard/ScoreObjective` | `tab_overlay.cpp`, `dllmain.cpp` |
| `aum` | `net/minecraft/scoreboard/Score` | `tab_overlay.cpp`, `dllmain.cpp` (via `GetObjectClass`) |
| `aul` | `net/minecraft/scoreboard/ScorePlayerTeam` | `tab_overlay.cpp`, `dllmain.cpp`, `utils.cpp` |
| `auq` | `net/minecraft/scoreboard/Team` | `tab_overlay.cpp`, `dllmain.cpp`, `utils.cpp` (sig. `formatPlayerName`) |

---

## Chat & texte

| Obf | MCP | Fichiers |
|-----|-----|----------|
| `eu` | `net/minecraft/util/IChatComponent` | `tab_overlay.cpp`, `utils.cpp`, `jni_helpers.cpp` |
| `eu$a` | `net/minecraft/util/IChatComponent$Serializer` | `dllmain.cpp`, `jni_helpers.cpp` |
| `fa` | `net/minecraft/util/ChatComponentText` | `utils.cpp`, `jni_helpers.cpp` |
| `ez` | `net/minecraft/util/ChatStyle` | `utils.cpp` |
| `a` | `net/minecraft/util/EnumChatFormatting` | `utils.cpp` |
| `jy` | `net/minecraft/util/ResourceLocation` | `utils.cpp`, `jni_helpers.cpp`, `tab_overlay.cpp` |

---

## Items & inventaire

| Obf | MCP | Fichiers |
|-----|-----|----------|
| `zx` | `net/minecraft/item/ItemStack` | `anticheat.cpp`, `utils.cpp`, `dllmain.cpp` |
| `zw` | `net/minecraft/item/Item` | `anticheat.cpp`, `utils.cpp` |
| `og` | `net/minecraft/inventory/IInventory` | `dllmain.cpp` (`lowerChestInventory`) |
| `xi` | `net/minecraft/inventory/Container` | `dllmain.cpp` |
| `yg` | `net/minecraft/inventory/Slot` | `dllmain.cpp` |

---

## Authlib (Mojang)

| Obf | MCP | Fichiers |
|-----|-----|----------|
| *(non obfusqué)* | `com/mojang/authlib/GameProfile` | `tab_overlay.cpp`, `anticheat.cpp`, `utils.cpp` |
| *(non obfusqué)* | `com/mojang/authlib/minecraft/MinecraftProfileTexture$Type` | `denick.cpp` |

---

## JDK / utilitaires Java

| Classe | Fichiers |
|--------|----------|
| `java/lang/Object` | `colored_hitboxes.cpp`, `dllmain.cpp` |
| `java/lang/Class` | `colored_hitboxes.cpp`, `dllmain.cpp` |
| `java/lang/ClassLoader` | `colored_hitboxes.cpp`, `dllmain.cpp` |
| `java/lang/Runnable` | `FrenchifyHitboxInstaller.java`, `colored_hitboxes.cpp` |
| `java/util/Collection` | `tab_overlay.cpp` |
| `java/util/Iterator` | `tab_overlay.cpp` |
| `java/util/List` | `colored_hitboxes.cpp`, `anticheat.cpp`, `utils.cpp`, `dllmain.cpp` |
| `java/util/Map` | `denick.cpp` |
| `java/util/UUID` | `tab_overlay.cpp`, `anticheat.cpp`, `utils.cpp` |

---

## LWJGL (GUI Java uniquement)

| Classe | Fichiers |
|--------|----------|
| `org/lwjgl/input/Mouse` | `FrenchifyGui4.java`, `FrenchifyGui4Lunar.java` |
| `org/lwjgl/input/Keyboard` | `FrenchifyGui4.java`, `FrenchifyGui4Lunar.java` |

---

## Champs Minecraft fréquents (`ave` / `Minecraft`)

| Obf | MCP | Type JNI | Fichiers |
|-----|-----|----------|----------|
| `A` | `getMinecraft` | `()Lave;` | `tab_overlay.cpp`, `dllmain.cpp` |
| `h` | `thePlayer` | `Lbew;` | partout |
| `f` | `theWorld` | `Lbdb;` | `anticheat.cpp`, `utils.cpp`, `denick.cpp` |
| `q` | `ingameGUI` | `Lavo;` | `dllmain.cpp`, `jni_helpers.cpp`, `tab_overlay.cpp` |
| `m` | `currentScreen` | `Laxu;` | `dllmain.cpp` |
| `c` | `playerController` | `Lbda;` | `dllmain.cpp` |
| `g` | `renderGlobal` | `Lbfr;` | `colored_hitboxes.cpp` |
| `k` | `fontRendererObj` | `Lavn;` | `tab_overlay.cpp` |
| `R` | `renderEngine` | `Lbmj;` | `tab_overlay.cpp` |
| `u` | `getNetHandler` | `()Lbcy;` | `anticheat.cpp`, `utils.cpp` |
| `W` | `getSoundHandler` | `()Lbpz;` | `jni_helpers.cpp` |
| `ab` | `getSkinManager` | `()Lbnp;` | `denick.cpp` |
| `af` | `getRenderManager` | `()Lbiu;` | `colored_hitboxes.cpp` |
| `Y` | `timer` | `Lavl;` | `colored_hitboxes.cpp` |
| `d` / `e` | `displayWidth` / `displayHeight` | `I` | `tab_overlay.cpp` |

---

## Entité (`pk` / `Entity`) — anticheat & hitboxes

| Obf | MCP | Fichiers |
|-----|-----|----------|
| `s` / `t` / `u` | `posX` / `posY` / `posZ` | `anticheat.cpp`, `utils.cpp`, `colored_hitboxes.cpp` |
| `z` | `rotationPitch` | `anticheat.cpp`, `colored_hitboxes.cpp` |
| `y` | `rotationYaw` | `colored_hitboxes.cpp` |
| `c` | `entityId` | `anticheat.cpp`, `colored_hitboxes.cpp` |
| `ae` | `riddenByEntity` | `anticheat.cpp` |
| `ar` / `as` | `isSwingInProgress` / `swingProgressInt` | `anticheat.cpp` |
| `av` / `aw` | `isSneaking` / `isSprinting` | `anticheat.cpp` |
| `bW` / `bS` | `isBlocking` / `isUsingItem` | `anticheat.cpp` |
| `bZ` / `bA` | `getHeldItem` | `anticheat.cpp`, `utils.cpp` |
| `bn` | `getHealth` | `anticheat.cpp` |
| `H` | `getDataWatcher` | `anticheat.cpp` |
| `cd` | `getGameProfile` | `anticheat.cpp`, `utils.cpp`, `tab_overlay.cpp` |
| `bO` | `getTeam` | `utils.cpp` |
| `b` | `getPlayerInfo` | `utils.cpp` |
| `e_` | `getName` | `utils.cpp` |
| `f_` | `getDisplayName` | `utils.cpp` |
| `S` / `aR` | `getCollisionBoundingBox` / `getEntityBoundingBox` | `colored_hitboxes.cpp` |
| `ax` | `isInvisible` | `colored_hitboxes.cpp` |
| `Z` | `hurtResistantTime` | `anticheat.cpp` |

### `pr` / `EntityLivingBase`

| Obf | MCP | Fichiers |
|-----|-----|----------|
| `au` / `av` | `hurtTime` / `maxHurtTime` | `anticheat.cpp` |
| `aO` | `recentlyHit` | `anticheat.cpp` |
| `aw` | `attackedAtYaw` | `anticheat.cpp` |

### `pz` / `DataWatcher`

| Obf | MCP | Fichiers |
|-----|-----|----------|
| `d` | `getWatchableObjectFloat` | `anticheat.cpp` |

---

## World (`bdb` / `WorldClient`)

| Obf | MCP | Fichiers |
|-----|-----|----------|
| `j` | `playerEntities` | `anticheat.cpp`, `utils.cpp`, `denick.cpp`, `colored_hitboxes.cpp` |
| `f` | `loadedEntityList` | `colored_hitboxes.cpp` |
| `Z` | `getScoreboard` | `utils.cpp` |

---

## Scoreboard — méthodes clés

| Classe | Obf | MCP | Sig. JNI |
|--------|-----|-----|----------|
| `Scoreboard` | `a` | `getObjectiveInDisplaySlot` | `(I)Lauk;` |
| `Scoreboard` | `h` | `getPlayersTeam` | `(Ljava/lang/String;)Laul;` |
| `Scoreboard` | `i` | `getSortedScores` | `(Lauk;)Ljava/util/Collection;` |
| `ScorePlayerTeam` | `a` | `formatPlayerName` | `(Lauq;Ljava/lang/String;)Ljava/lang/String;` |
| `ScorePlayerTeam` | `l` | `getChatFormat` | `()La;` |
| `Score` | `e` / `c` | `getPlayerName` / `getScorePoints` | `dllmain.cpp`, `tab_overlay.cpp` |
| `EntityPlayerSP` | `cp` | `getWorldScoreboard` | `()Lauo;` |

---

## Chat — méthodes clés

| Classe | Obf | MCP | Sig. JNI |
|--------|-----|-----|----------|
| `GuiIngame` | `d` | `getChatGUI` | `()Lavt;` |
| `GuiNewChat` | `a` | `printChatMessage` | `(Leu;)V` |
| `GuiNewChat` | `h` / `i` | `chatLines` / `drawnChatLines` | `Ljava/util/List;` |
| `IChatComponent` | `c` / `d` | `getUnformattedText` / `getFormattedText` | `()Ljava/lang/String;` |
| `IChatComponent$Serializer` | `a` | `componentToJson` / `jsonToComponent` | `(Leu;)Ljava/lang/String;` / `(Ljava/lang/String;)Leu;` |
| `ChatLine` | `a` | `getChatComponent` | `()Leu;` |

---

## Items

| Classe | Obf | MCP | Sig. JNI |
|--------|-----|-----|----------|
| `Item` | `b` | `getIdFromItem` | `(Lzw;)I` |
| `ItemStack` | `b` | `getItem` | `()Lzw;` |

---

## Voir aussi

- [`LUNAR_CLIENT_OBF.md`](LUNAR_CLIENT_OBF.md) — obfuscation **Lunar Client** (LMP Tiny V2, bridges `com/moonsworth/lunar/…`)
- [`LUNAR_MAPPINGS.md`](LUNAR_MAPPINGS.md) — workflow build `release_lunar` (champs/méthodes MCP)
