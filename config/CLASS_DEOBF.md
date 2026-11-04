# CLASS_DEOBF — vanilla obf → MCP (Frenchify JNI)

Généré: `2026-06-14T10:55:54Z` · source: `src` · MC 1.8.9

**30 classes** · **67 champs** (51 résolus) · **106 méthodes** (76 résolues)

Regénérer: `python scripts/generate_class_deobf.py`

## Classes

| obf | MCP | vanilla | lunar | fichiers |
|-----|-----|---------|-------|----------|
| `a` | `net.minecraft.util.EnumChatFormatting` | `env->FindClass("a")` | `LunarFindClass(env, "net/minecraft/util/EnumChatFormatting")` | utils.cpp |
| `aug` | `net.minecraft.util.AxisAlignedBB` | `env->FindClass("aug")` | `LunarFindClass(env, "net/minecraft/util/AxisAlignedBB")` | colored_hitboxes.cpp |
| `aul` | `net.minecraft.scoreboard.ScorePlayerTeam` | `env->FindClass("aul")` | `LunarFindClass(env, "net/minecraft/scoreboard/ScorePlayerTeam")` | dllmain.cpp, tab_overlay.cpp |
| `ave` | `net.minecraft.client.Minecraft` | `env->FindClass("ave")` | `LunarFindClass(env, "net/minecraft/client/Minecraft")` | dllmain.cpp, tab_overlay.cpp |
| `avn` | `net.minecraft.client.gui.FontRenderer` | `env->FindClass("avn")` | `LunarFindClass(env, "net/minecraft/client/gui/FontRenderer")` | tab_overlay.cpp |
| `avo` | `net.minecraft.client.gui.GuiIngame` | `env->FindClass("avo")` | `LunarFindClass(env, "net/minecraft/client/gui/GuiIngame")` | tab_overlay.cpp |
| `avp` | `net.minecraft.client.gui.Gui` | `env->FindClass("avp")` | `LunarFindClass(env, "net/minecraft/client/gui/Gui")` | tab_overlay.cpp |
| `avr` | `net.minecraft.client.gui.ScaledResolution` | `env->FindClass("avr")` | `LunarFindClass(env, "net/minecraft/client/gui/ScaledResolution")` | tab_overlay.cpp |
| `awh` | `net.minecraft.client.gui.GuiPlayerTabOverlay` | `env->FindClass("awh")` | `LunarFindClass(env, "net/minecraft/client/gui/GuiPlayerTabOverlay")` | tab_overlay.cpp |
| `awv` | `net.minecraft.client.gui.GuiChat` | `env->FindClass("awv")` | `LunarFindClass(env, "net/minecraft/client/gui/GuiChat")` | dllmain.cpp |
| `ayl` | `net.minecraft.client.gui.inventory.GuiContainer` | `env->FindClass("ayl")` | `LunarFindClass(env, "net/minecraft/client/gui/inventory/GuiContainer")` | dllmain.cpp |
| `ayr` | `net.minecraft.client.gui.inventory.GuiChest` | `env->FindClass("ayr")` | `LunarFindClass(env, "net/minecraft/client/gui/inventory/GuiChest")` | dllmain.cpp |
| `bcy` | `net.minecraft.client.network.NetHandlerPlayClient` | `env->FindClass("bcy")` | `LunarFindClass(env, "net/minecraft/client/network/NetHandlerPlayClient")` | tab_overlay.cpp |
| `bdc` | `net.minecraft.client.network.NetworkPlayerInfo` | `env->FindClass("bdc")` | `LunarFindClass(env, "net/minecraft/client/network/NetworkPlayerInfo")` | tab_overlay.cpp |
| `bet` | `net.minecraft.client.entity.AbstractClientPlayer` | `env->FindClass("bet")` | `LunarFindClass(env, "net/minecraft/client/entity/AbstractClientPlayer")` | colored_hitboxes.cpp, denick.cpp, utils.cpp |
| `bew` | `net.minecraft.client.entity.EntityPlayerSP` | `env->FindClass("bew")` | `LunarFindClass(env, "net/minecraft/client/entity/EntityPlayerSP")` | tab_overlay.cpp |
| `bex` | `net.minecraft.client.entity.EntityOtherPlayerMP` | `env->FindClass("bex")` | `LunarFindClass(env, "net/minecraft/client/entity/EntityOtherPlayerMP")` | colored_hitboxes.cpp |
| `biu` | `net.minecraft.client.renderer.entity.RenderManager` | `env->FindClass("biu")` | `LunarFindClass(env, "net/minecraft/client/renderer/entity/RenderManager")` | colored_hitboxes.cpp |
| `bmj` | `net.minecraft.client.renderer.texture.TextureManager` | `env->FindClass("bmj")` | `LunarFindClass(env, "net/minecraft/client/renderer/texture/TextureManager")` | tab_overlay.cpp |
| `bnp` | `net.minecraft.client.resources.SkinManager` | `env->FindClass("bnp")` | `LunarFindClass(env, "net/minecraft/client/resources/SkinManager")` | denick.cpp |
| `bpf` | `net.minecraft.client.audio.PositionedSoundRecord` | `env->FindClass("bpf")` | `LunarFindClass(env, "net/minecraft/client/audio/PositionedSoundRecord")` | jni_helpers.cpp |
| `eu` | `net.minecraft.util.IChatComponent` | `env->FindClass("eu")` | `LunarFindClass(env, "net/minecraft/util/IChatComponent")` | tab_overlay.cpp, utils.cpp |
| `eu$a` | `net.minecraft.util.IChatComponent$Serializer` | `env->FindClass("eu$a")` | `LunarFindClass(env, "net/minecraft/util/IChatComponent$Serializer")` | dllmain.cpp, jni_helpers.cpp |
| `ez` | `net.minecraft.util.ChatStyle` | `env->FindClass("ez")` | `LunarFindClass(env, "net/minecraft/util/ChatStyle")` | utils.cpp |
| `fa` | `net.minecraft.util.ChatComponentText` | `env->FindClass("fa")` | `LunarFindClass(env, "net/minecraft/util/ChatComponentText")` | jni_helpers.cpp, utils.cpp |
| `jy` | `net.minecraft.util.ResourceLocation` | `env->FindClass("jy")` | `LunarFindClass(env, "net/minecraft/util/ResourceLocation")` | jni_helpers.cpp |
| `pk` | `net.minecraft.entity.Entity` | `env->FindClass("pk")` | `LunarFindClass(env, "net/minecraft/entity/Entity")` | anticheat.cpp, colored_hitboxes.cpp, utils.cpp |
| `pr` | `net.minecraft.entity.EntityLivingBase` | `env->FindClass("pr")` | `LunarFindClass(env, "net/minecraft/entity/EntityLivingBase")` | anticheat.cpp, utils.cpp |
| `pz` | `net.minecraft.entity.DataWatcher` | `env->FindClass("pz")` | `LunarFindClass(env, "net/minecraft/entity/DataWatcher")` | anticheat.cpp |
| `wn` | `net.minecraft.entity.player.EntityPlayer` | `env->FindClass("wn")` | `LunarFindClass(env, "net/minecraft/entity/player/EntityPlayer")` | denick.cpp |

## Champs

| owner | obf | MCP | vanilla JNI | lunar JNI | fichiers |
|-------|-----|-----|-------------|-----------|----------|
| `?` | `api_aurora` | `api_aurora` ? | `GetStaticFieldID(cls, "api_aurora", "Ljava/lang/String;")` | `GetStaticFieldID(cls, "api_aurora", "Ljava/lang/String;")` | dllmain.cpp |
| `?` | `api_hypixel` | `api_hypixel` ? | `GetStaticFieldID(cls, "api_hypixel", "Ljava/lang/String;")` | `GetStaticFieldID(cls, "api_hypixel", "Ljava/lang/String;")` | dllmain.cpp |
| `?` | `api_seraph` | `api_seraph` ? | `GetStaticFieldID(cls, "api_seraph", "Ljava/lang/String;")` | `GetStaticFieldID(cls, "api_seraph", "Ljava/lang/String;")` | dllmain.cpp |
| `?` | `api_winstreak` | `api_winstreak` ? | `GetStaticFieldID(cls, "api_winstreak", "Ljava/lang/String;")` | `GetStaticFieldID(cls, "api_winstreak", "Ljava/lang/String;")` | dllmain.cpp |
| `?` | `autoGL` | `autoGL` ? | `GetStaticFieldID(cls, "autoGL", "Z")` | `GetStaticFieldID(cls, "autoGL", "Z")` | dllmain.cpp |
| `?` | `autoWho` | `autoWho` ? | `GetStaticFieldID(cls, "autoWho", "Z")` | `GetStaticFieldID(cls, "autoWho", "Z")` | dllmain.cpp |
| `?` | `delegate` | `delegate` ? | `GetFieldID(cls, "delegate", "Lavt;")` | `GetFieldID(cls, "delegate", "Lnet/minecraft/client/gui/GuiNewChat;")` | jni_helpers.cpp |
| `?` | `enabled` | `enabled` ? | `GetStaticFieldID(cls, "enabled", "Z")` | `GetStaticFieldID(cls, "enabled", "Z")` | dllmain.cpp |
| `?` | `fastBuy` | `fastBuy` ? | `GetStaticFieldID(cls, "fastBuy", "Z")` | `GetStaticFieldID(cls, "fastBuy", "Z")` | dllmain.cpp |
| `?` | `gl_message` | `gl_message` ? | `GetStaticFieldID(cls, "gl_message", "Ljava/lang/String;")` | `GetStaticFieldID(cls, "gl_message", "Ljava/lang/String;")` | dllmain.cpp |
| `?` | `overlayGamemodeActive` | `overlayGamemodeActive` ? | `GetStaticFieldID(cls, "overlayGamemodeActive", "Ljava/lang/String;")` | `GetStaticFieldID(cls, "overlayGamemodeActive", "Ljava/lang/String;")` | dllmain.cpp, jni_helpers.cpp |
| `?` | `overlayGamemodeEdit` | `overlayGamemodeEdit` ? | `GetStaticFieldID(cls, "overlayGamemodeEdit", "Ljava/lang/String;")` | `GetStaticFieldID(cls, "overlayGamemodeEdit", "Ljava/lang/String;")` | dllmain.cpp, jni_helpers.cpp |
| `?` | `overlayOpacity` | `overlayOpacity` ? | `GetStaticFieldID(cls, "overlayOpacity", "I")` | `GetStaticFieldID(cls, "overlayOpacity", "I")` | dllmain.cpp, jni_helpers.cpp |
| `?` | `overlayResourceHeader` | `overlayResourceHeader` ? | `GetStaticFieldID(cls, "overlayResourceHeader", "Z")` | `GetStaticFieldID(cls, "overlayResourceHeader", "Z")` | dllmain.cpp |
| `?` | `partyDetector` | `partyDetector` ? | `GetStaticFieldID(cls, "partyDetector", "Z")` | `GetStaticFieldID(cls, "partyDetector", "Z")` | dllmain.cpp |
| `?` | `tabOverlay` | `tabOverlay` ? | `GetStaticFieldID(cls, "tabOverlay", "Z")` | `GetStaticFieldID(cls, "tabOverlay", "Z")` | dllmain.cpp |
| `net.minecraft.client.Minecraft` | `m` | `currentScreen` ✓ | `GetFieldID(cls, "m", "Laxu;")` | `GetFieldID(cls, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;")` | dllmain.cpp |
| `net.minecraft.client.Minecraft` | `e` | `displayHeight` ✓ | `GetFieldID(cls, "e", "I")` | `GetFieldID(cls, "displayHeight", "I")` | colored_hitboxes.cpp, tab_overlay.cpp |
| `net.minecraft.client.Minecraft` | `d` | `displayWidth` ✓ | `GetFieldID(cls, "d", "I")` | `GetFieldID(cls, "displayWidth", "I")` | colored_hitboxes.cpp, tab_overlay.cpp |
| `net.minecraft.client.Minecraft` | `k` | `fontRendererObj` ✓ | `GetFieldID(cls, "k", "Lavn;")` | `GetFieldID(cls, "fontRendererObj", "Lnet/minecraft/client/gui/FontRenderer;")` | tab_overlay.cpp |
| `net.minecraft.client.Minecraft` | `q` | `ingameGUI` ✓ | `GetFieldID(cls, "q", "Lavo;")` | `GetFieldID(cls, "ingameGUI", "Lnet/minecraft/client/gui/GuiIngame;")` | dllmain.cpp, jni_helpers.cpp, tab_overlay.cpp |
| `net.minecraft.client.Minecraft` | `c` | `playerController` ✓ | `GetFieldID(cls, "c", "Lbda;")` | `GetFieldID(cls, "playerController", "Lnet/minecraft/client/multiplayer/PlayerControllerMP;")` | dllmain.cpp |
| `net.minecraft.client.Minecraft` | `R` | `renderEngine` ✓ | `GetFieldID(cls, "R", "Lbmj;")` | `GetFieldID(cls, "renderEngine", "Lnet/minecraft/client/renderer/texture/TextureManager;")` | tab_overlay.cpp |
| `net.minecraft.client.Minecraft` | `g` | `renderGlobal` ✓ | `GetFieldID(cls, "g", "Lbfr;")` | `GetFieldID(cls, "renderGlobal", "Lnet/minecraft/client/renderer/RenderGlobal;")` | colored_hitboxes.cpp |
| `net.minecraft.client.Minecraft` | `h` | `thePlayer` ✓ | `GetFieldID(cls, "h", "Lbew;")` | `GetFieldID(cls, "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;")` | anticheat.cpp, colored_hitboxes.cpp, dllmain.cpp, tab_overlay.cpp, utils.cpp |
| `net.minecraft.client.Minecraft` | `f` | `theWorld` ✓ | `GetFieldID(cls, "f", "Lbdb;")` | `GetFieldID(cls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;")` | anticheat.cpp, denick.cpp, utils.cpp |
| `net.minecraft.client.Minecraft` | `Y` | `timer` ✓ | `GetFieldID(cls, "Y", "Lavl;")` | `GetFieldID(cls, "timer", "Lnet/minecraft/util/Timer;")` | colored_hitboxes.cpp |
| `net.minecraft.client.entity.EntityPlayerSP` | `c` | `mc` ✓ | `GetFieldID(cls, "c", "I")` | `GetFieldID(cls, "mc", "I")` | anticheat.cpp |
| `net.minecraft.client.entity.EntityPlayerSP` | `a` | `sendQueue` ✓ | `GetFieldID(cls, "a", "Lbcy;")` | `GetFieldID(cls, "sendQueue", "Lnet/minecraft/client/network/NetHandlerPlayClient;")` | tab_overlay.cpp |
| `net.minecraft.client.gui.GuiChat` | `a` | `inputField` ✓ | `GetFieldID(cls, "a", "Lavw;")` | `GetFieldID(cls, "inputField", "Lnet/minecraft/client/gui/GuiTextField;")` | dllmain.cpp |
| `net.minecraft.client.gui.GuiIngame` | `v` | `overlayPlayerList` ✓ | `GetFieldID(cls, "v", "Lawh;")` | `GetFieldID(cls, "overlayPlayerList", "Lnet/minecraft/client/gui/GuiPlayerTabOverlay;")` | dllmain.cpp, tab_overlay.cpp |
| `net.minecraft.client.gui.GuiNewChat` | `h` | `chatLines` ✓ | `GetFieldID(cls, "h", "Ljava/util/List;")` | `GetFieldID(cls, "chatLines", "Ljava/util/List;")` | dllmain.cpp, jni_helpers.cpp |
| `net.minecraft.client.gui.GuiNewChat` | `i` | `drawnChatLines` ✓ | `GetFieldID(cls, "i", "Ljava/util/List;")` | `GetFieldID(cls, "drawnChatLines", "Ljava/util/List;")` | jni_helpers.cpp |
| `net.minecraft.client.gui.GuiPlayerTabOverlay` | `h` | `footer` ✓ | `GetFieldID(cls, "h", "Leu;")` | `GetFieldID(cls, "footer", "Lnet/minecraft/util/IChatComponent;")` | tab_overlay.cpp |
| `net.minecraft.client.gui.inventory.GuiChest` | `w` | `lowerChestInventory` ✓ | `GetFieldID(cls, "w", "Log;")` | `GetFieldID(cls, "lowerChestInventory", "Lnet/minecraft/inventory/IInventory;")` | dllmain.cpp |
| `net.minecraft.client.gui.inventory.GuiContainer` | `h` | `inventorySlots` ✓ | `GetFieldID(cls, "h", "Lxi;")` | `GetFieldID(cls, "inventorySlots", "Lnet/minecraft/inventory/Container;")` | dllmain.cpp |
| `net.minecraft.client.gui.inventory.GuiContainer` | `u` | `theSlot` ✓ | `GetFieldID(cls, "u", "Lyg;")` | `GetFieldID(cls, "theSlot", "Lnet/minecraft/inventory/Slot;")` | dllmain.cpp |
| `net.minecraft.client.renderer.entity.RenderManager` | `f` | `playerViewX` ✓ | `GetFieldID(cls, "f", "F")` | `GetFieldID(cls, "playerViewX", "F")` | colored_hitboxes.cpp |
| `net.minecraft.client.renderer.entity.RenderManager` | `e` | `playerViewY` ✓ | `GetFieldID(cls, "e", "F")` | `GetFieldID(cls, "playerViewY", "F")` | colored_hitboxes.cpp |
| `net.minecraft.entity.Entity` | `ae` | `chunkCoordX` ✓ | `GetFieldID(cls, "ae", "Lpk;")` | `GetFieldID(cls, "chunkCoordX", "Lnet/minecraft/entity/Entity;")` | anticheat.cpp |
| `net.minecraft.entity.Entity` | `c` | `entityId` ✓ | `GetFieldID(cls, "c", "I")` | `GetFieldID(cls, "entityId", "I")` | anticheat.cpp, colored_hitboxes.cpp |
| `net.minecraft.entity.Entity` | `ar` | `entityRiderPitchDelta` ✓ | `GetFieldID(cls, "ar", "Z")` | `GetFieldID(cls, "entityRiderPitchDelta", "Z")` | anticheat.cpp |
| `net.minecraft.entity.Entity` | `as` | `entityRiderYawDelta` ✓ | `GetFieldID(cls, "as", "I")` | `GetFieldID(cls, "entityRiderYawDelta", "I")` | anticheat.cpp |
| `net.minecraft.entity.Entity` | `Z` | `hurtResistantTime` ✓ | `GetFieldID(cls, "Z", "I")` | `GetFieldID(cls, "hurtResistantTime", "I")` | anticheat.cpp |
| `net.minecraft.entity.Entity` | `P` | `lastTickPosX` ✓ | `GetFieldID(cls, "P", "D")` | `GetFieldID(cls, "lastTickPosX", "D")` | colored_hitboxes.cpp |
| `net.minecraft.entity.Entity` | `Q` | `lastTickPosY` ✓ | `GetFieldID(cls, "Q", "D")` | `GetFieldID(cls, "lastTickPosY", "D")` | colored_hitboxes.cpp |
| `net.minecraft.entity.Entity` | `R` | `lastTickPosZ` ✓ | `GetFieldID(cls, "R", "D")` | `GetFieldID(cls, "lastTickPosZ", "D")` | colored_hitboxes.cpp |
| `net.minecraft.entity.Entity` | `s` | `posX` ✓ | `GetFieldID(cls, "s", "D")` | `GetFieldID(cls, "posX", "D")` | anticheat.cpp, colored_hitboxes.cpp, utils.cpp |
| `net.minecraft.entity.Entity` | `t` | `posY` ✓ | `GetFieldID(cls, "t", "D")` | `GetFieldID(cls, "posY", "D")` | anticheat.cpp, colored_hitboxes.cpp, utils.cpp |
| `net.minecraft.entity.Entity` | `u` | `posZ` ✓ | `GetFieldID(cls, "u", "D")` | `GetFieldID(cls, "posZ", "D")` | anticheat.cpp, colored_hitboxes.cpp, utils.cpp |
| `net.minecraft.entity.Entity` | `l` | `riddenByEntity` ✓ | `GetFieldID(cls, "l", "F")` | `GetFieldID(cls, "riddenByEntity", "F")` | colored_hitboxes.cpp |
| `net.minecraft.entity.Entity` | `m` | `ridingEntity` ✓ | `GetFieldID(cls, "m", "F")` | `GetFieldID(cls, "ridingEntity", "F")` | colored_hitboxes.cpp |
| `net.minecraft.entity.Entity` | `z` | `rotationPitch` ✓ | `GetFieldID(cls, "z", "F")` | `GetFieldID(cls, "rotationPitch", "F")` | anticheat.cpp, colored_hitboxes.cpp |
| `net.minecraft.entity.Entity` | `y` | `rotationYaw` ✓ | `GetFieldID(cls, "y", "F")` | `GetFieldID(cls, "rotationYaw", "F")` | colored_hitboxes.cpp |
| `net.minecraft.entity.EntityLivingBase` | `au` | `hurtTime` ✓ | `GetFieldID(cls, "au", "I")` | `GetFieldID(cls, "hurtTime", "I")` | anticheat.cpp |
| `net.minecraft.entity.EntityLivingBase` | `aO` | `recentlyHit` ✓ | `GetFieldID(cls, "aO", "I")` | `GetFieldID(cls, "recentlyHit", "I")` | anticheat.cpp |
| `net.minecraft.inventory.Container` | `d` | `windowId` ✓ | `GetFieldID(cls, "d", "I")` | `GetFieldID(cls, "windowId", "I")` | dllmain.cpp |
| `net.minecraft.inventory.Slot` | `e` | `slotNumber` ✓ | `GetFieldID(cls, "e", "I")` | `GetFieldID(cls, "slotNumber", "I")` | dllmain.cpp |
| `net.minecraft.util.AxisAlignedBB` | `d` | `maxX` ✓ | `GetFieldID(cls, "d", "D")` | `GetFieldID(cls, "maxX", "D")` | colored_hitboxes.cpp |
| `net.minecraft.util.AxisAlignedBB` | `e` | `maxY` ✓ | `GetFieldID(cls, "e", "D")` | `GetFieldID(cls, "maxY", "D")` | colored_hitboxes.cpp |
| `net.minecraft.util.AxisAlignedBB` | `f` | `maxZ` ✓ | `GetFieldID(cls, "f", "D")` | `GetFieldID(cls, "maxZ", "D")` | colored_hitboxes.cpp |
| `net.minecraft.util.AxisAlignedBB` | `a` | `minX` ✓ | `GetFieldID(cls, "a", "D")` | `GetFieldID(cls, "minX", "D")` | colored_hitboxes.cpp |
| `net.minecraft.util.AxisAlignedBB` | `b` | `minY` ✓ | `GetFieldID(cls, "b", "D")` | `GetFieldID(cls, "minY", "D")` | colored_hitboxes.cpp |
| `net.minecraft.util.AxisAlignedBB` | `c` | `minZ` ✓ | `GetFieldID(cls, "c", "D")` | `GetFieldID(cls, "minZ", "D")` | colored_hitboxes.cpp |
| `net.minecraft.util.Timer` | `c` | `renderPartialTicks` ✓ | `GetFieldID(cls, "c", "F")` | `GetFieldID(cls, "renderPartialTicks", "F")` | colored_hitboxes.cpp |
| `net.minecraft.world.World` | `f` | `loadedEntityList` ✓ | `GetFieldID(cls, "f", "Ljava/util/List;")` | `GetFieldID(cls, "loadedEntityList", "Ljava/util/List;")` | colored_hitboxes.cpp |
| `net.minecraft.world.World` | `j` | `playerEntities` ✓ | `GetFieldID(cls, "j", "Ljava/util/List;")` | `GetFieldID(cls, "playerEntities", "Ljava/util/List;")` | anticheat.cpp, colored_hitboxes.cpp, denick.cpp, utils.cpp |

## Méthodes

| owner | obf | MCP | vanilla JNI | lunar JNI | fichiers |
|-------|-----|-----|-------------|-----------|----------|
| `?` | `a` | `drawOutlinedBoundingBox` ✓ | `GetStaticMethodID(cls, "a", "(Laug;IIII)V")` | `GetStaticMethodID(cls, "drawOutlinedBoundingBox", "(Lnet/minecraft/util/AxisAlignedBB;IIII)V")` | colored_hitboxes.cpp |
| `?` | `entries` | `entries` ? | `GetMethodID(cls, "entries", "()Ljava/util/Set;")` | `GetMethodID(cls, "entries", "()Ljava/util/Set;")` | denick.cpp |
| `?` | `get` | `get` ? | `GetMethodID(cls, "get", "(I)Ljava/lang/Object;")` | `GetMethodID(cls, "get", "(I)Ljava/lang/Object;")` | anticheat.cpp, colored_hitboxes.cpp, denick.cpp, dllmain.cpp, utils.cpp |
| `?` | `get` | `get` ? | `GetMethodID(cls, "get", "(Ljava/lang/String;)Ljava/util/Collection;")` | `GetMethodID(cls, "get", "(Ljava/lang/String;)Ljava/util/Collection;")` | denick.cpp |
| `?` | `get` | `get` ? | `GetMethodID(cls, "get", "(Ljava/lang/Object;)Ljava/lang/Object;")` | `GetMethodID(cls, "get", "(Ljava/lang/Object;)Ljava/lang/Object;")` | denick.cpp |
| `?` | `getClass` | `getClass` ? | `GetMethodID(cls, "getClass", "()Ljava/lang/Class;")` | `GetMethodID(cls, "getClass", "()Ljava/lang/Class;")` | colored_hitboxes.cpp, dllmain.cpp |
| `?` | `getClassLoader` | `getClassLoader` ? | `GetMethodID(cls, "getClassLoader", "()Ljava/lang/ClassLoader;")` | `GetMethodID(cls, "getClassLoader", "()Ljava/lang/ClassLoader;")` | colored_hitboxes.cpp, dllmain.cpp |
| `?` | `getId` | `getId` ? | `GetMethodID(cls, "getId", "()Ljava/util/UUID;")` | `GetMethodID(cls, "getId", "()Ljava/util/UUID;")` | anticheat.cpp, tab_overlay.cpp, utils.cpp |
| `?` | `b` | `getIdFromItem` ✓ | `GetStaticMethodID(cls, "b", "(Lzw;)I")` | `GetStaticMethodID(cls, "getIdFromItem", "(Lnet/minecraft/item/Item;)I")` | anticheat.cpp, utils.cpp |
| `?` | `getKey` | `getKey` ? | `GetMethodID(cls, "getKey", "()Ljava/lang/Object;")` | `GetMethodID(cls, "getKey", "()Ljava/lang/Object;")` | denick.cpp |
| `?` | `getName` | `getName` ? | `GetMethodID(cls, "getName", "()Ljava/lang/String;")` | `GetMethodID(cls, "getName", "()Ljava/lang/String;")` | anticheat.cpp, denick.cpp, tab_overlay.cpp, utils.cpp |
| `?` | `getOverlayColumnsConfig` | `getOverlayColumnsConfig` ? | `GetStaticMethodID(cls, "getOverlayColumnsConfig", "()Ljava/lang/String;")` | `GetStaticMethodID(cls, "getOverlayColumnsConfig", "()Ljava/lang/String;")` | dllmain.cpp |
| `?` | `getValue` | `getValue` ? | `GetMethodID(cls, "getValue", "()Ljava/lang/String;")` | `GetMethodID(cls, "getValue", "()Ljava/lang/String;")` | denick.cpp |
| `?` | `getValue` | `getValue` ? | `GetMethodID(cls, "getValue", "()Ljava/lang/Object;")` | `GetMethodID(cls, "getValue", "()Ljava/lang/Object;")` | denick.cpp |
| `?` | `hasNext` | `hasNext` ? | `GetMethodID(cls, "hasNext", "()Z")` | `GetMethodID(cls, "hasNext", "()Z")` | anticheat.cpp, denick.cpp, dllmain.cpp, tab_overlay.cpp |
| `?` | `iterator` | `iterator` ? | `GetMethodID(cls, "iterator", "()Ljava/util/Iterator;")` | `GetMethodID(cls, "iterator", "()Ljava/util/Iterator;")` | anticheat.cpp, denick.cpp, dllmain.cpp, tab_overlay.cpp |
| `?` | `loadClass` | `loadClass` ? | `GetMethodID(cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;")` | `GetMethodID(cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;")` | colored_hitboxes.cpp, dllmain.cpp |
| `?` | `loadOverlayColumnsForEditMode` | `loadOverlayColumnsForEditMode` ? | `GetStaticMethodID(cls, "loadOverlayColumnsForEditMode", "()V")` | `GetStaticMethodID(cls, "loadOverlayColumnsForEditMode", "()V")` | dllmain.cpp |
| `?` | `next` | `next` ? | `GetMethodID(cls, "next", "()Ljava/lang/Object;")` | `GetMethodID(cls, "next", "()Ljava/lang/Object;")` | anticheat.cpp, denick.cpp, dllmain.cpp, tab_overlay.cpp |
| `?` | `remove` | `remove` ? | `GetMethodID(cls, "remove", "(I)Ljava/lang/Object;")` | `GetMethodID(cls, "remove", "(I)Ljava/lang/Object;")` | jni_helpers.cpp |
| `?` | `setOverlayGamemodeActive` | `setOverlayGamemodeActive` ? | `GetStaticMethodID(cls, "setOverlayGamemodeActive", "(Ljava/lang/String;)V")` | `GetStaticMethodID(cls, "setOverlayGamemodeActive", "(Ljava/lang/String;)V")` | dllmain.cpp |
| `?` | `size` | `size` ? | `GetMethodID(cls, "size", "()I")` | `GetMethodID(cls, "size", "()I")` | anticheat.cpp, colored_hitboxes.cpp, denick.cpp, dllmain.cpp, jni_helpers.cpp, utils.cpp |
| `?` | `snapshotOverlayColumnsEditMode` | `snapshotOverlayColumnsEditMode` ? | `GetStaticMethodID(cls, "snapshotOverlayColumnsEditMode", "()V")` | `GetStaticMethodID(cls, "snapshotOverlayColumnsEditMode", "()V")` | jni_helpers.cpp |
| `?` | `toString` | `toString` ? | `GetMethodID(cls, "toString", "()Ljava/lang/String;")` | `GetMethodID(cls, "toString", "()Ljava/lang/String;")` | anticheat.cpp, tab_overlay.cpp |
| `?` | `values` | `values` ? | `GetMethodID(cls, "values", "()Ljava/util/Collection;")` | `GetMethodID(cls, "values", "()Ljava/util/Collection;")` | denick.cpp |
| `net.minecraft.client.Minecraft` | `a` | `addScheduledTask` ✓ | `GetMethodID(cls, "a", "(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;")` | `GetMethodID(cls, "addScheduledTask", "(Ljava/lang/Runnable;)Lcom/google/common/util/concurrent/ListenableFuture;")` | colored_hitboxes.cpp |
| `net.minecraft.client.Minecraft` | `a` | `displayGuiScreen` ✓ | `GetMethodID(cls, "a", "(Laxu;)V")` | `GetMethodID(cls, "displayGuiScreen", "(Lnet/minecraft/client/gui/GuiScreen;)V")` | dllmain.cpp |
| `net.minecraft.client.Minecraft` | `A` | `getMinecraft` ✓ | `GetStaticMethodID(cls, "A", "()Lave;")` | `GetStaticMethodID(cls, "getMinecraft", "()Lnet/minecraft/client/Minecraft;")` | dllmain.cpp, tab_overlay.cpp |
| `net.minecraft.client.Minecraft` | `u` | `getNetHandler` ✓ | `GetMethodID(cls, "u", "()Lbcy;")` | `GetMethodID(cls, "getNetHandler", "()Lnet/minecraft/client/network/NetHandlerPlayClient;")` | anticheat.cpp, utils.cpp |
| `net.minecraft.client.Minecraft` | `af` | `getRenderManager` ✓ | `GetMethodID(cls, "af", "()Lbiu;")` | `GetMethodID(cls, "getRenderManager", "()Lnet/minecraft/client/renderer/entity/RenderManager;")` | colored_hitboxes.cpp |
| `net.minecraft.client.Minecraft` | `ab` | `getSkinManager` ✓ | `GetMethodID(cls, "ab", "()Lbnp;")` | `GetMethodID(cls, "getSkinManager", "()Lnet/minecraft/client/resources/SkinManager;")` | denick.cpp |
| `net.minecraft.client.Minecraft` | `W` | `getSoundHandler` ✓ | `GetMethodID(cls, "W", "()Lbpz;")` | `GetMethodID(cls, "getSoundHandler", "()Lnet/minecraft/client/audio/SoundHandler;")` | jni_helpers.cpp |
| `net.minecraft.client.audio.PositionedSoundRecord` | `a` | `create` ✓ | `GetStaticMethodID(cls, "a", "(Ljy;F)Lbpf;")` | `GetStaticMethodID(cls, "create", "(Lnet/minecraft/util/ResourceLocation;F)Lnet/minecraft/client/audio/PositionedSoundRecord;")` | jni_helpers.cpp |
| `net.minecraft.client.audio.PositionedSoundRecord` | `a` | `create` ✓ | `GetStaticMethodID(cls, "a", "(Ljy;)Lbpf;")` | `GetStaticMethodID(cls, "create", "(Lnet/minecraft/util/ResourceLocation;)Lnet/minecraft/client/audio/PositionedSoundRecord;")` | jni_helpers.cpp |
| `net.minecraft.client.audio.SoundHandler` | `a` | `playSound` ✓ | `GetMethodID(cls, "a", "(Lbpj;)V")` | `GetMethodID(cls, "playSound", "(Lnet/minecraft/client/audio/ISound;)V")` | jni_helpers.cpp |
| `net.minecraft.client.entity.AbstractClientPlayer` | `i` | `getLocationSkin` ✓ | `GetMethodID(cls, "i", "()Ljy;")` | `GetMethodID(cls, "getLocationSkin", "()Lnet/minecraft/util/ResourceLocation;")` | denick.cpp |
| `net.minecraft.client.entity.EntityPlayerSP` | `bS` | `bS` ? | `GetMethodID(cls, "bS", "()Z")` | `GetMethodID(cls, "bS", "()Z")` | utils.cpp |
| `net.minecraft.client.entity.EntityPlayerSP` | `cd` | `getGameProfile` ✓ | `GetMethodID(cls, "cd", "()Lcom/mojang/authlib/GameProfile;")` | `GetMethodID(cls, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;")` | tab_overlay.cpp |
| `net.minecraft.client.entity.EntityPlayerSP` | `cp` | `getWorldScoreboard` ✓ | `GetMethodID(cls, "cp", "()Lauo;")` | `GetMethodID(cls, "getWorldScoreboard", "()Lnet/minecraft/scoreboard/Scoreboard;")` | dllmain.cpp, tab_overlay.cpp |
| `net.minecraft.client.entity.EntityPlayerSP` | `e` | `sendChatMessage` ✓ | `GetMethodID(cls, "e", "(Ljava/lang/String;)V")` | `GetMethodID(cls, "sendChatMessage", "(Ljava/lang/String;)V")` | dllmain.cpp |
| `net.minecraft.client.gui.FontRenderer` | `a` | `drawStringWithShadow` ✓ | `GetMethodID(cls, "a", "(Ljava/lang/String;FFI)I")` | `GetMethodID(cls, "drawStringWithShadow", "(Ljava/lang/String;FFI)I")` | tab_overlay.cpp |
| `net.minecraft.client.gui.FontRenderer` | `a` | `getStringWidth` ✓ | `GetMethodID(cls, "a", "(Ljava/lang/String;)I")` | `GetMethodID(cls, "getStringWidth", "(Ljava/lang/String;)I")` | tab_overlay.cpp |
| `net.minecraft.client.gui.Gui` | `a` | `drawModalRectWithCustomSizedTexture` ✓ | `GetStaticMethodID(cls, "a", "(IIFFIIFF)V")` | `GetStaticMethodID(cls, "drawModalRectWithCustomSizedTexture", "(IIFFIIFF)V")` | tab_overlay.cpp |
| `net.minecraft.client.gui.GuiIngame` | `d` | `getChatGUI` ✓ | `GetMethodID(cls, "d", "()Lavt;")` | `GetMethodID(cls, "getChatGUI", "()Lnet/minecraft/client/gui/GuiNewChat;")` | dllmain.cpp, jni_helpers.cpp |
| `net.minecraft.client.gui.GuiListExtended` | `a` | `a` ? | `GetMethodID(cls, "a", "()Leu;")` | `GetMethodID(cls, "a", "()Lnet/minecraft/util/IChatComponent;")` | dllmain.cpp |
| `net.minecraft.client.gui.GuiListExtended` | `b` | `b` ? | `GetMethodID(cls, "b", "()I")` | `GetMethodID(cls, "b", "()I")` | dllmain.cpp |
| `net.minecraft.client.gui.GuiNewChat` | `a` | `addToSentMessages` ✓ | `GetMethodID(cls, "a", "(Ljava/lang/String;)V")` | `GetMethodID(cls, "addToSentMessages", "(Ljava/lang/String;)V")` | dllmain.cpp |
| `net.minecraft.client.gui.GuiNewChat` | `a` | `printChatMessage` ✓ | `GetMethodID(cls, "a", "(Leu;)V")` | `GetMethodID(cls, "printChatMessage", "(Lnet/minecraft/util/IChatComponent;)V")` | jni_helpers.cpp |
| `net.minecraft.client.gui.GuiTextField` | `b` | `getText` ✓ | `GetMethodID(cls, "b", "()Ljava/lang/String;")` | `GetMethodID(cls, "getText", "()Ljava/lang/String;")` | dllmain.cpp |
| `net.minecraft.client.gui.ScaledResolution` | `b` | `getScaledHeight` ✓ | `GetMethodID(cls, "b", "()I")` | `GetMethodID(cls, "getScaledHeight", "()I")` | tab_overlay.cpp |
| `net.minecraft.client.gui.ScaledResolution` | `a` | `getScaledWidth` ✓ | `GetMethodID(cls, "a", "()I")` | `GetMethodID(cls, "getScaledWidth", "()I")` | tab_overlay.cpp |
| `net.minecraft.client.multiplayer.PlayerControllerMP` | `a` | `windowClick` ✓ | `GetMethodID(cls, "a", "(IIIILwn;)Lzx;")` | `GetMethodID(cls, "windowClick", "(IIIILnet/minecraft/entity/player/EntityPlayer;)Lnet/minecraft/item/ItemStack;")` | dllmain.cpp |
| `net.minecraft.client.network.NetHandlerPlayClient` | `a` | `getPlayerInfo` ✓ | `GetMethodID(cls, "a", "(Ljava/util/UUID;)Lbdc;")` | `GetMethodID(cls, "getPlayerInfo", "(Ljava/util/UUID;)Lnet/minecraft/client/network/NetworkPlayerInfo;")` | anticheat.cpp, utils.cpp |
| `net.minecraft.client.network.NetHandlerPlayClient` | `d` | `getPlayerInfoMap` ✓ | `GetMethodID(cls, "d", "()Ljava/util/Collection;")` | `GetMethodID(cls, "getPlayerInfoMap", "()Ljava/util/Collection;")` | anticheat.cpp, tab_overlay.cpp |
| `net.minecraft.client.network.NetworkPlayerInfo` | `k` | `getDisplayName` ✓ | `GetMethodID(cls, "k", "()Leu;")` | `GetMethodID(cls, "getDisplayName", "()Lnet/minecraft/util/IChatComponent;")` | anticheat.cpp, tab_overlay.cpp |
| `net.minecraft.client.network.NetworkPlayerInfo` | `a` | `getGameProfile` ✓ | `GetMethodID(cls, "a", "()Lcom/mojang/authlib/GameProfile;")` | `GetMethodID(cls, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;")` | anticheat.cpp, tab_overlay.cpp |
| `net.minecraft.client.network.NetworkPlayerInfo` | `g` | `getLocationSkin` ✓ | `GetMethodID(cls, "g", "()Ljy;")` | `GetMethodID(cls, "getLocationSkin", "()Lnet/minecraft/util/ResourceLocation;")` | tab_overlay.cpp |
| `net.minecraft.client.network.NetworkPlayerInfo` | `i` | `getPlayerTeam` ✓ | `GetMethodID(cls, "i", "()Laul;")` | `GetMethodID(cls, "getPlayerTeam", "()Lnet/minecraft/scoreboard/ScorePlayerTeam;")` | anticheat.cpp, tab_overlay.cpp, utils.cpp |
| `net.minecraft.client.network.NetworkPlayerInfo` | `c` | `getResponseTime` ✓ | `GetMethodID(cls, "c", "()I")` | `GetMethodID(cls, "getResponseTime", "()I")` | tab_overlay.cpp |
| `net.minecraft.client.network.NetworkPlayerInfo` | `i` | `i` ? | `GetMethodID(cls, "i", "()Lauq;")` | `GetMethodID(cls, "i", "()Lnet/minecraft/scoreboard/Team;")` | utils.cpp |
| `net.minecraft.client.renderer.entity.RenderManager` | `b` | `renderDebugBoundingBox` ✓ | `GetMethodID(cls, "b", "(Lpk;DDDFF)V")` | `GetMethodID(cls, "renderDebugBoundingBox", "(Lnet/minecraft/entity/Entity;DDDFF)V")` | colored_hitboxes.cpp |
| `net.minecraft.client.renderer.texture.TextureManager` | `a` | `bindTexture` ✓ | `GetMethodID(cls, "a", "(Ljy;)V")` | `GetMethodID(cls, "bindTexture", "(Lnet/minecraft/util/ResourceLocation;)V")` | tab_overlay.cpp |
| `net.minecraft.entity.DataWatcher` | `d` | `getWatchableObjectFloat` ✓ | `GetMethodID(cls, "d", "(I)F")` | `GetMethodID(cls, "getWatchableObjectFloat", "(I)F")` | anticheat.cpp |
| `net.minecraft.entity.Entity` | `bS` | `bS` ? | `GetMethodID(cls, "bS", "()Z")` | `GetMethodID(cls, "bS", "()Z")` | anticheat.cpp |
| `net.minecraft.entity.Entity` | `bW` | `bW` ? | `GetMethodID(cls, "bW", "()Z")` | `GetMethodID(cls, "bW", "()Z")` | anticheat.cpp |
| `net.minecraft.entity.Entity` | `S` | `getCollisionBoundingBox` ✓ | `GetMethodID(cls, "S", "()Laug;")` | `GetMethodID(cls, "getCollisionBoundingBox", "()Lnet/minecraft/util/AxisAlignedBB;")` | colored_hitboxes.cpp |
| `net.minecraft.entity.Entity` | `bZ` | `getCurrentEquippedItem` ✓ | `GetMethodID(cls, "bZ", "()Lzx;")` | `GetMethodID(cls, "getCurrentEquippedItem", "()Lnet/minecraft/item/ItemStack;")` | anticheat.cpp, utils.cpp |
| `net.minecraft.entity.Entity` | `H` | `getDataWatcher` ✓ | `GetMethodID(cls, "H", "()Lpz;")` | `GetMethodID(cls, "getDataWatcher", "()Lnet/minecraft/entity/DataWatcher;")` | anticheat.cpp |
| `net.minecraft.entity.Entity` | `f_` | `getDisplayName` ✓ | `GetMethodID(cls, "f_", "()Leu;")` | `GetMethodID(cls, "getDisplayName", "()Lnet/minecraft/util/IChatComponent;")` | utils.cpp |
| `net.minecraft.entity.Entity` | `aR` | `getEntityBoundingBox` ✓ | `GetMethodID(cls, "aR", "()Laug;")` | `GetMethodID(cls, "getEntityBoundingBox", "()Lnet/minecraft/util/AxisAlignedBB;")` | colored_hitboxes.cpp |
| `net.minecraft.entity.Entity` | `cd` | `getGameProfile` ✓ | `GetMethodID(cls, "cd", "()Lcom/mojang/authlib/GameProfile;")` | `GetMethodID(cls, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;")` | anticheat.cpp, utils.cpp |
| `net.minecraft.entity.Entity` | `bn` | `getHealth` ✓ | `GetMethodID(cls, "bn", "()F")` | `GetMethodID(cls, "getHealth", "()F")` | anticheat.cpp |
| `net.minecraft.entity.Entity` | `bA` | `getHeldItem` ✓ | `GetMethodID(cls, "bA", "()Lzx;")` | `GetMethodID(cls, "getHeldItem", "()Lnet/minecraft/item/ItemStack;")` | anticheat.cpp, utils.cpp |
| `net.minecraft.entity.Entity` | `e_` | `getName` ✓ | `GetMethodID(cls, "e_", "()Ljava/lang/String;")` | `GetMethodID(cls, "getName", "()Ljava/lang/String;")` | utils.cpp |
| `net.minecraft.entity.Entity` | `b` | `getPlayerInfo` ✓ | `GetMethodID(cls, "b", "()Lbdc;")` | `GetMethodID(cls, "getPlayerInfo", "()Lnet/minecraft/client/network/NetworkPlayerInfo;")` | utils.cpp |
| `net.minecraft.entity.Entity` | `bO` | `getTeam` ✓ | `GetMethodID(cls, "bO", "()Lauq;")` | `GetMethodID(cls, "getTeam", "()Lnet/minecraft/scoreboard/Team;")` | utils.cpp |
| `net.minecraft.entity.Entity` | `ax` | `isInvisible` ✓ | `GetMethodID(cls, "ax", "()Z")` | `GetMethodID(cls, "isInvisible", "()Z")` | colored_hitboxes.cpp |
| `net.minecraft.entity.Entity` | `av` | `isSneaking` ✓ | `GetMethodID(cls, "av", "()Z")` | `GetMethodID(cls, "isSneaking", "()Z")` | anticheat.cpp |
| `net.minecraft.entity.Entity` | `aw` | `isSprinting` ✓ | `GetMethodID(cls, "aw", "()Z")` | `GetMethodID(cls, "isSprinting", "()Z")` | anticheat.cpp |
| `net.minecraft.entity.EntityLivingBase` | `bn` | `getHealth` ✓ | `GetMethodID(cls, "bn", "()F")` | `GetMethodID(cls, "getHealth", "()F")` | anticheat.cpp |
| `net.minecraft.entity.EntityLivingBase` | `bO` | `getTeam` ✓ | `GetMethodID(cls, "bO", "()Lauq;")` | `GetMethodID(cls, "getTeam", "()Lnet/minecraft/scoreboard/Team;")` | utils.cpp |
| `net.minecraft.entity.player.EntityPlayer` | `cd` | `getGameProfile` ✓ | `GetMethodID(cls, "cd", "()Lcom/mojang/authlib/GameProfile;")` | `GetMethodID(cls, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;")` | denick.cpp |
| `net.minecraft.inventory.Container` | `a` | `a` ? | `GetMethodID(cls, "a", "(I)Lzx;")` | `GetMethodID(cls, "a", "(I)Lnet/minecraft/item/ItemStack;")` | dllmain.cpp |
| `net.minecraft.item.ItemStack` | `q` | `getDisplayName` ✓ | `GetMethodID(cls, "q", "()Ljava/lang/String;")` | `GetMethodID(cls, "getDisplayName", "()Ljava/lang/String;")` | dllmain.cpp |
| `net.minecraft.item.ItemStack` | `b` | `getItem` ✓ | `GetMethodID(cls, "b", "()Lzw;")` | `GetMethodID(cls, "getItem", "()Lnet/minecraft/item/Item;")` | anticheat.cpp, utils.cpp |
| `net.minecraft.scoreboard.Score` | `e` | `getPlayerName` ✓ | `GetMethodID(cls, "e", "()Ljava/lang/String;")` | `GetMethodID(cls, "getPlayerName", "()Ljava/lang/String;")` | dllmain.cpp, tab_overlay.cpp |
| `net.minecraft.scoreboard.Score` | `c` | `getScorePoints` ✓ | `GetMethodID(cls, "c", "()I")` | `GetMethodID(cls, "getScorePoints", "()I")` | dllmain.cpp, tab_overlay.cpp |
| `net.minecraft.scoreboard.ScoreObjective` | `d` | `getDisplayName` ✓ | `GetMethodID(cls, "d", "()Ljava/lang/String;")` | `GetMethodID(cls, "getDisplayName", "()Ljava/lang/String;")` | dllmain.cpp |
| `net.minecraft.scoreboard.ScorePlayerTeam` | `a` | `formatPlayerName` ✓ | `GetStaticMethodID(cls, "a", "(Lauq;Ljava/lang/String;)Ljava/lang/String;")` | `GetStaticMethodID(cls, "formatPlayerName", "(Lnet/minecraft/scoreboard/Team;Ljava/lang/String;)Ljava/lang/String;")` | dllmain.cpp, tab_overlay.cpp |
| `net.minecraft.scoreboard.ScorePlayerTeam` | `l` | `getChatFormat` ✓ | `GetMethodID(cls, "l", "()La;")` | `GetMethodID(cls, "getChatFormat", "()Lnet/minecraft/util/EnumChatFormatting;")` | utils.cpp |
| `net.minecraft.scoreboard.ScorePlayerTeam` | `d` | `getMembershipCollection` ✓ | `GetMethodID(cls, "d", "()Ljava/util/Collection;")` | `GetMethodID(cls, "getMembershipCollection", "()Ljava/util/Collection;")` | anticheat.cpp |
| `net.minecraft.scoreboard.ScorePlayerTeam` | `b` | `getRegisteredName` ✓ | `GetMethodID(cls, "b", "()Ljava/lang/String;")` | `GetMethodID(cls, "getRegisteredName", "()Ljava/lang/String;")` | utils.cpp |
| `net.minecraft.scoreboard.Scoreboard` | `a` | `getObjectiveInDisplaySlot` ✓ | `GetMethodID(cls, "a", "(I)Lauk;")` | `GetMethodID(cls, "getObjectiveInDisplaySlot", "(I)Lnet/minecraft/scoreboard/ScoreObjective;")` | dllmain.cpp, tab_overlay.cpp |
| `net.minecraft.scoreboard.Scoreboard` | `h` | `getPlayersTeam` ✓ | `GetMethodID(cls, "h", "(Ljava/lang/String;)Laul;")` | `GetMethodID(cls, "getPlayersTeam", "(Ljava/lang/String;)Lnet/minecraft/scoreboard/ScorePlayerTeam;")` | dllmain.cpp, tab_overlay.cpp, utils.cpp |
| `net.minecraft.scoreboard.Scoreboard` | `i` | `getSortedScores` ✓ | `GetMethodID(cls, "i", "(Lauk;)Ljava/util/Collection;")` | `GetMethodID(cls, "getSortedScores", "(Lnet/minecraft/scoreboard/ScoreObjective;)Ljava/util/Collection;")` | dllmain.cpp, tab_overlay.cpp |
| `net.minecraft.util.ChatComponentText` | `a` | `setChatStyle` ✓ | `GetMethodID(cls, "a", "(Lez;)Leu;")` | `GetMethodID(cls, "setChatStyle", "(Lnet/minecraft/util/ChatStyle;)Lnet/minecraft/util/IChatComponent;")` | utils.cpp |
| `net.minecraft.util.ChatStyle` | `a` | `setColor` ✓ | `GetMethodID(cls, "a", "(La;)Lez;")` | `GetMethodID(cls, "setColor", "(Lnet/minecraft/util/EnumChatFormatting;)Lnet/minecraft/util/ChatStyle;")` | utils.cpp |
| `net.minecraft.util.EnumChatFormatting` | `b` | `getColorIndex` ✓ | `GetMethodID(cls, "b", "()I")` | `GetMethodID(cls, "getColorIndex", "()I")` | utils.cpp |
| `net.minecraft.util.IChatComponent` | `a` | `appendSibling` ✓ | `GetMethodID(cls, "a", "(Leu;)Leu;")` | `GetMethodID(cls, "appendSibling", "(Lnet/minecraft/util/IChatComponent;)Lnet/minecraft/util/IChatComponent;")` | utils.cpp |
| `net.minecraft.util.IChatComponent` | `f` | `createCopy` ✓ | `GetMethodID(cls, "f", "()Leu;")` | `GetMethodID(cls, "createCopy", "()Lnet/minecraft/util/IChatComponent;")` | utils.cpp |
| `net.minecraft.util.IChatComponent` | `d` | `getFormattedText` ✓ | `GetMethodID(cls, "d", "()Ljava/lang/String;")` | `GetMethodID(cls, "getFormattedText", "()Ljava/lang/String;")` | tab_overlay.cpp, utils.cpp |
| `net.minecraft.util.IChatComponent` | `a` | `getSiblings` ✓ | `GetMethodID(cls, "a", "()Ljava/util/List;")` | `GetMethodID(cls, "getSiblings", "()Ljava/util/List;")` | utils.cpp |
| `net.minecraft.util.IChatComponent` | `c` | `getUnformattedText` ✓ | `GetMethodID(cls, "c", "()Ljava/lang/String;")` | `GetMethodID(cls, "getUnformattedText", "()Ljava/lang/String;")` | anticheat.cpp, tab_overlay.cpp, utils.cpp |
| `net.minecraft.util.IChatComponent$Serializer` | `a` | `componentToJson` ✓ | `GetStaticMethodID(cls, "a", "(Leu;)Ljava/lang/String;")` | `GetStaticMethodID(cls, "componentToJson", "(Lnet/minecraft/util/IChatComponent;)Ljava/lang/String;")` | dllmain.cpp |
| `net.minecraft.util.IChatComponent$Serializer` | `a` | `jsonToComponent` ✓ | `GetStaticMethodID(cls, "a", "(Ljava/lang/String;)Leu;")` | `GetStaticMethodID(cls, "jsonToComponent", "(Ljava/lang/String;)Lnet/minecraft/util/IChatComponent;")` | jni_helpers.cpp |
| `net.minecraft.world.World` | `Z` | `getScoreboard` ✓ | `GetMethodID(cls, "Z", "()Lauo;")` | `GetMethodID(cls, "getScoreboard", "()Lnet/minecraft/scoreboard/Scoreboard;")` | utils.cpp |
