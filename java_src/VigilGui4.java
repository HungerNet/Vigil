import org.lwjgl.input.Mouse;
import org.lwjgl.input.Keyboard;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.FontRenderer;
import net.minecraft.client.gui.Gui;
import net.minecraft.client.gui.GuiScreen;
public class VigilGui4 extends GuiScreen {
    public static boolean autoWho = false;
    public static boolean partyDetector = false;
    public static boolean partyQueueDetector = false;
    public static boolean bedDisconnectTimer = false;
    public static boolean holdRdEnabled = false;
    public static int holdRdKeyCode = Keyboard.KEY_LMENU;
    public static boolean quickbuyEnabled = false;
    public static int quickbuyDelayMs = 100;
    public static final String[] QUICKBUY_IDS = {
        "wool","stone_sword","iron_sword","golden_apple","fire_charge","tnt","ender_pearl",
        "pickaxe","axe","shears","chainmail_boots","iron_boots",
        "upg iron_sword","upg iron_chestplate","upg iron_pickaxe","upg golden_pickaxe","upg diamond_boots",
        "diamond_sword","stick","arrow","diamond_boots"
    };
    public static final String[] QUICKBUY_LABELS = {
        "Wool","Stone Sword","Iron Sword","Golden Apple","Fireball","TNT","Ender Pearl",
        "Pickaxe","Axe","Shears","Chainmail Armor","Iron Armor",
        "Sharpness","Protection","Mining Fatigue","Haste","Feather Falling",
        "Diamond Sword","Knockback Stick","Arrows","Diamond Armor"
    };
    public static int[] quickbuyKeys = new int[QUICKBUY_IDS.length];
    public static int[] quickbuySlots = new int[QUICKBUY_IDS.length];
    public static boolean[] quickbuyTurbo = new boolean[QUICKBUY_IDS.length];
    public static String quickbuyBinds = "";
    public static boolean numberDenicker = false;
    public static boolean autoGL = false;
    public static boolean fastBuy = false;
    public static boolean coloredHitboxes = false;
    public static boolean dragonHitboxes = false;
    public static boolean fireballHitboxes = false;
    public static String fireballHitboxColor = "c";
    public static int hitboxThickness = 1;
    public static boolean arrowDistance = false;
    public static boolean tradeIndicator = false;
    public static boolean george = false;
    public static int hkGeorge = 0;
    public static boolean nickQol = false;
    public static int hkNickQol = 0;
    public static boolean nickQolPendingSend = false;
    public static boolean suppressHotkeys = false;
    public static int tradeIndicatorPosX = 520;
    public static int tradeIndicatorPosY = 470;
    public static boolean hudEditMode = false;
    public static int hkAutoWho = 0;
    public static int hkPartyDetector = 0;
    public static int hkNumberDenicker = 0;
    public static int hkAutoGL = 0;
    public static int hkFastBuy = 0;
    public static int hkColoredHitboxes = 0;
    public static int hkDragonHitboxes = 0;
    public static int hkArrowDistance = 0;
    public static int hkBedDisconnectTimer = 0;
    public static int hkTradeIndicator = 0;
    public static int hkFireballHitboxes = 0;
    public static boolean tabOverlay = true;
    public static boolean overlayResourceHeader = true;
    public static boolean overlayResourceDiamonds = true;
    public static boolean overlayResourceEmeralds = true;
    public static String overlayResourceLocation = "tab";
    public static int overlayOpacity = 60;
    public static String overlayThemeColor = "0F1316";
    public static String configThemeColor = "1A2227";
    public static String configTextColor = "D6DEE4";
    private static final String[] OVERLAY_GAMEMODES = {
        "bedwars", "skywars", "duel", "bedwarsduels", "murdermystery"
    };
    private static final String[] OVERLAY_GAMEMODE_LABELS = {
        "Bedwars", "SkyWars", "Duel", "BW Duels", "Murder"
    };
    public static String overlayGamemodeEdit = "bedwars";
    public static String overlayGamemodeActive = "unknown";
    private static String overlayColumnsBedwars = "";
    private static String overlayColumnsSkywars = "";
    private static String overlayColumnsDuel = "";
    private static String overlayColumnsBedwarsduels = "";
    private static String overlayColumnsMurdermystery = "";
    private static final int MAX_OVERLAY_COLUMNS = 18;
    private static final String DEFAULT_COLS_BEDWARS =
        "name,hp,stars,stars_new,fkdr,wlr,kdr,swordKD,bblr,index,wins,finalKills,bedsBroken,requeuePct,winstreak,seens,session,ping,pingvar,seraph";
    private static final String DEFAULT_COLS_SKYWARS =
        "name,hp,sw_stars,sw_kdr,sw_wlr,sw_wins,sw_kills,seens,session,ping,pingvar,seraph";
    private static final String DEFAULT_COLS_DUEL =
        "name,hp,duel_wins,duel_wlr,duel_kdr,seens,session,ping,pingvar,seraph";
    private static final String DEFAULT_COLS_BWDUELS =
        "name,hp,bwd_wlr,bwd_index,seens,session,ping,pingvar,seraph";
    private static final String DEFAULT_COLS_MURDER =
        "name,hp,mm_kdr,mm_wlr,mm_kills,mm_wins,mm_icon,seens,session,ping,pingvar,seraph";
    private static final String[] BW_COLUMN_IDS = {
        "stars", "stars_new", "fkdr", "wlr", "kdr", "swordKD", "bblr", "index", "wins",
        "finalKills", "bedsBroken", "requeuePct", "winstreak", "seens", "session", "ping", "pingvar", "seraph"
    };
    private static final String[] BW_COLUMN_LABELS = {
        "Stars", "Stars New", "FKDR", "WLR", "KDR", "SwordKD", "BBLR", "Index", "Wins",
        "FinalKills", "BedsBroken", "Requeue%", "Winstreak", "Seens", "Session", "Ping", "PingVar", "Seraph"
    };
    private static final String[] SW_COLUMN_IDS = {
        "sw_stars", "sw_kdr", "sw_wlr", "sw_wins", "sw_kills", "seens", "session", "ping", "pingvar", "seraph"
    };
    private static final String[] SW_COLUMN_LABELS = {
        "Stars (SW)", "KDR (SW)", "WLR (SW)", "Wins (SW)", "Kills (SW)", "Seens", "Session", "Ping", "PingVar", "Seraph"
    };
    private static final String[] DUEL_COLUMN_IDS = {
        "duel_wins", "duel_wlr", "duel_kdr", "seens", "session", "ping", "pingvar", "seraph"
    };
    private static final String[] DUEL_COLUMN_LABELS = {
        "Wins (Duel)", "WLR (Duel)", "KDR (Duel)", "Seens", "Session", "Ping", "PingVar", "Seraph"
    };
    private static final String[] BWD_COLUMN_IDS = {
        "bwd_wlr", "bwd_index", "seens", "session", "ping", "pingvar", "seraph"
    };
    private static final String[] BWD_COLUMN_LABELS = {
        "WLR (BW Duels)", "Index (BW Duels)", "Seens", "Session", "Ping", "PingVar", "Seraph"
    };
    private static final String[] MM_COLUMN_IDS = {
        "mm_kdr", "mm_wlr", "mm_kills", "mm_wins", "mm_icon", "seens", "session", "ping", "pingvar", "seraph"
    };
    private static final String[] MM_COLUMN_LABELS = {
        "KDR (MM)", "WLR (MM)", "Kills (MM)", "Wins (MM)", "Prefix (MM)", "Seens", "Session", "Ping", "PingVar", "Seraph"
    };
    public static boolean[] overlayColumnEnabled = {
        true, true, true, true, true, true, true, true, true, true,
        true, true, true, true, true, true, true, true
    };
    public static int[] overlayColumnOrder = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
        10, 11, 12, 13, 14, 15, 16, 17
    };
    public static boolean anticheat = false;
    public static boolean ac_flagsound = true;

    public static boolean ac_autoblock = false;
    public static boolean ac_legitscaff = false;
    public static boolean ac_scaffold = false;
    public static boolean ac_scaffoldb = false;
    public static boolean ac_noslow = false;

    public static String api_hypixel = "";
    public static String api_seraph = "";
    public static String api_aurora = ""; // Vega key (number denicker)
    public static boolean vegaProxy = false;
    public static String gl_message = "gg";
    public static boolean compactBlacklist = false;
    public static boolean chatOverlay = false;
    public static boolean ignoreTeammates = false;
    public static String chatOverlayMinStars = "100";
    public static String chatOverlayMinFkdr = "5";
    public static String chatOverlayMinSwKdr = "1.5";

    public static boolean sessionStats = false;
    public static boolean sessionStatsGame = true;
    public static boolean sessionStatsFinalKills = true;
    public static boolean sessionStatsBeds = true;
    public static boolean sessionStatsKills = true;
    public static boolean sessionStatsWins = true;
    public static boolean sessionStatsWinstreak = true;
    public static boolean sessionStatsSessionGames = true;
    public static boolean sessionStatsGameTime = true;
    public static boolean sessionStatsAvgTime = true;
    public static boolean sessionStatsSessionTime = true;
    public static boolean sessionStatsSlumberTickets = true;
    public static boolean sessionStatsTextShadow = false;
    public static boolean sessionStatsResetPending = false;
    public static int sessionStatsBgOpacity = 100;
    public static int sessionStatsScale = 100;
    public static int sessionStatsPosX = 1000;
    public static int sessionStatsPosY = 1000;

    private static final String[] THEMES = {"Settings", "Anticheat", "Utils", "Overlay", "Chat Overlay", "Session", "Experimental", "Blackchify"};
    public int themeScroll = 0;
    public static int selectedTheme = 1; // Default: Anticheat
    public int winX, winY, winW, winH;
    private static FontRenderer cachedFontRenderer = null;
    private int activeInput = 0;
    private int settingsScroll = 0;
    private int anticheatScroll = 0;
    private int utilsScroll = 0;
    private int overlayScroll = 0;
    private int chatOverlayScroll = 0;
    private int sessionStatsScroll = 0;
    private int experimentalScroll = 0;
    private int blackchifyScroll = 0;
    private int overlaySelectedIndex = -1;
    private boolean showHypixel = false;
    private boolean showSeraph = false;
    private boolean showAurora = false;
    private boolean draggingHitboxSlider = false;
    private int hitboxSliderTrackX = 0;
    private int hitboxSliderTrackW = 0;
    private static boolean listeningHoldRdKey = false;
    private static boolean listeningNickQolKey = false;
    private static boolean nickQolWasDown = false;
    private static int listeningQuickbuyIndex = -1;
    private static int listeningUtilsHk = -1;
    private static final String[] UTILS_HK_LABELS = {
        "AutoWho", "Party Detector", "Number Denicker", "Auto GL", "Fast Buy",
        "Colored Hitboxes", "Dragon Hitbox", "Shot Distance", "Bed DC Timer",
        "Trade Indicator", "Fireball Hitbox", "George"
    };
    private static final boolean[] utilsHkWasDown = new boolean[12];

    private static int getUtilsHotkey(int index) {
        switch (index) {
            case 0: return hkAutoWho;
            case 1: return hkPartyDetector;
            case 2: return hkNumberDenicker;
            case 3: return hkAutoGL;
            case 4: return hkFastBuy;
            case 5: return hkColoredHitboxes;
            case 6: return hkDragonHitboxes;
            case 7: return hkArrowDistance;
            case 8: return hkBedDisconnectTimer;
            case 9: return hkTradeIndicator;
            case 10: return hkFireballHitboxes;
            case 11: return hkGeorge;
            default: return 0;
        }
    }

    private static void setUtilsHotkey(int index, int keyCode) {
        switch (index) {
            case 0: hkAutoWho = keyCode; break;
            case 1: hkPartyDetector = keyCode; break;
            case 2: hkNumberDenicker = keyCode; break;
            case 3: hkAutoGL = keyCode; break;
            case 4: hkFastBuy = keyCode; break;
            case 5: hkColoredHitboxes = keyCode; break;
            case 6: hkDragonHitboxes = keyCode; break;
            case 7: hkArrowDistance = keyCode; break;
            case 8: hkBedDisconnectTimer = keyCode; break;
            case 9: hkTradeIndicator = keyCode; break;
            case 10: hkFireballHitboxes = keyCode; break;
            case 11: hkGeorge = keyCode; break;
            default: break;
        }
    }

    private static void toggleUtilsModule(int index) {
        switch (index) {
            case 0: autoWho = !autoWho; break;
            case 1: partyDetector = !partyDetector; break;
            case 2: numberDenicker = !numberDenicker; break;
            case 3: autoGL = !autoGL; break;
            case 4: fastBuy = !fastBuy; break;
            case 5: coloredHitboxes = !coloredHitboxes; break;
            case 6: dragonHitboxes = !dragonHitboxes; break;
            case 7: arrowDistance = !arrowDistance; break;
            case 8: bedDisconnectTimer = !bedDisconnectTimer; break;
            case 9: tradeIndicator = !tradeIndicator; break;
            case 10: fireballHitboxes = !fireballHitboxes; break;
            case 11: george = !george; break;
            default: break;
        }
    }

    private static String utilsHotkeyLabel(int index) {
        if (listeningUtilsHk == index) return "...";
        int kc = getUtilsHotkey(index);
        if (kc <= 0) return "NONE";
        try {
            String n = Keyboard.getKeyName(kc);
            if (n != null && n.length() > 0) return n;
        } catch (Throwable ignored) {}
        return "#" + kc;
    }

    private static boolean isUtilsHotkeyListening() {
        return listeningUtilsHk >= 0 || listeningHoldRdKey || listeningNickQolKey || listeningQuickbuyIndex >= 0;
    }

    /** Called from native each tick — edge-triggered toggle for Utils hotkeys. */
    public static void pollUtilsHotkeys() {
        if (hudEditMode || isUtilsHotkeyListening()) return;
        for (int i = 0; i < UTILS_HK_LABELS.length; i++) {
            int kc = getUtilsHotkey(i);
            boolean down = false;
            if (kc > 0) {
                try { down = Keyboard.isKeyDown(kc); } catch (Throwable ignored) { down = false; }
            }
            // Keep edge state in sync while typing in chat so release/press after chat doesn't false-fire.
            if (!suppressHotkeys && down && !utilsHkWasDown[i]) toggleUtilsModule(i);
            utilsHkWasDown[i] = down;
        }
    }

    public static void pollNickQolHotkey() {
        if (!nickQol || hudEditMode || isUtilsHotkeyListening() || listeningNickQolKey) return;
        boolean down = false;
        if (hkNickQol > 0) {
            try { down = Keyboard.isKeyDown(hkNickQol); } catch (Throwable ignored) { down = false; }
        }
        if (!suppressHotkeys && down && !nickQolWasDown) nickQolPendingSend = true;
        nickQolWasDown = down;
    }

    public static boolean isHoldRdHotkeyDown() {
        if (!holdRdEnabled || listeningHoldRdKey || listeningQuickbuyIndex >= 0 || listeningUtilsHk >= 0) return false;
        if (holdRdKeyCode <= 0) return false;
        try {
            return Keyboard.isKeyDown(holdRdKeyCode);
        } catch (Throwable ignored) {
            return false;
        }
    }

    public static boolean isQuickbuyKeyDown(int index) {
        if (!quickbuyEnabled || listeningQuickbuyIndex >= 0 || listeningHoldRdKey || listeningUtilsHk >= 0) return false;
        if (index < 0 || index >= quickbuyKeys.length) return false;
        int kc = quickbuyKeys[index];
        if (kc == 0) return false;
        try {
            // Negative codes = mouse buttons encoded as (-100 - button), same as vanilla KeyBinding.
            if (kc < 0) {
                int btn = -100 - kc;
                if (btn < 0 || btn > 15) return false;
                return Mouse.isButtonDown(btn);
            }
            return Keyboard.isKeyDown(kc);
        } catch (Throwable t) {
            return false;
        }
    }

    private static String quickbuyKeyLabel(int index) {
        if (index < 0 || index >= quickbuyKeys.length || quickbuyKeys[index] == 0) return "NONE";
        int kc = quickbuyKeys[index];
        if (kc < 0) {
            int btn = -100 - kc;
            if (btn == 0) return "LMB";
            if (btn == 1) return "RMB";
            if (btn == 2) return "MMB";
            return "MOUSE" + (btn + 1);
        }
        try {
            String n = Keyboard.getKeyName(kc);
            if (n != null && n.length() > 0) return n;
        } catch (Throwable ignored) {}
        return "#" + kc;
    }

    private static boolean isQuickbuyUpgrade(int index) {
        return index >= 0 && index < QUICKBUY_IDS.length && QUICKBUY_IDS[index].startsWith("upg ");
    }

    public static String serializeQuickbuyBinds() {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < QUICKBUY_IDS.length; i++) {
            if (i > 0) sb.append(';');
            sb.append(QUICKBUY_IDS[i]).append(':').append(quickbuyKeys[i])
              .append(':').append(quickbuySlots[i]).append(':').append(quickbuyTurbo[i] ? 1 : 0);
        }
        return sb.toString();
    }

    public static void applyQuickbuyBinds(String raw) {
        if (raw == null || raw.isEmpty()) return;
        String[] parts = raw.split(";");
        for (String p : parts) {
            String[] t = p.split(":");
            if (t.length < 4) continue;
            for (int i = 0; i < QUICKBUY_IDS.length; i++) {
                if (!QUICKBUY_IDS[i].equals(t[0])) continue;
                try {
                    quickbuyKeys[i] = Integer.parseInt(t[1]);
                    quickbuySlots[i] = Integer.parseInt(t[2]);
                    quickbuyTurbo[i] = "1".equals(t[3]);
                } catch (Throwable ignored) {}
                break;
            }
        }
    }

    public static void setQuickbuyBinds(String s) {
        quickbuyBinds = s;
        applyQuickbuyBinds(s);
    }

    public static void applyQuickbuyBindsFromField() {
        applyQuickbuyBinds(quickbuyBinds);
    }

    private static void syncQuickbuyBinds() {
        quickbuyBinds = serializeQuickbuyBinds();
    }

    private static void cycleQuickbuyDelay() {
        int[] delays = {0, 50, 100, 150, 200, 250, 300, 350, 400};
        int idx = 0;
        for (int i = 0; i < delays.length; i++) {
            if (delays[i] == quickbuyDelayMs) {
                idx = i;
                break;
            }
        }
        quickbuyDelayMs = delays[(idx + 1) % delays.length];
    }

    private static int normalizeOpacity(int v) {
        if (v < 0) return 0;
        if (v > 95) return 95;
        return v;
    }

    private static int normalizeHitboxThickness(int v) {
        if (v < 1) return 1;
        if (v > 10) return 10;
        return v;
    }

    private static String sanitizeHexColor(String s) {
        if (s == null) return "0F1316";
        String in = s.trim();
        if (in.startsWith("#")) in = in.substring(1);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < in.length(); i++) {
            char c = in.charAt(i);
            boolean ok = (c >= '0' && c <= '9') ||
                         (c >= 'a' && c <= 'f') ||
                         (c >= 'A' && c <= 'F');
            if (ok) sb.append(Character.toUpperCase(c));
        }
        if (sb.length() > 6) sb.setLength(6);
        return sb.toString();
    }

    private static int parseHexColorInt(String s, int fallback) {
        String hex = sanitizeHexColor(s);
        if (hex.length() != 6) return fallback;
        try {
            return Integer.parseInt(hex, 16) & 0xFFFFFF;
        } catch (Throwable ignored) {
            return fallback;
        }
    }

    private static int mulRgb(int rgb, int percent) {
        if (percent < 0) percent = 0;
        if (percent > 200) percent = 200;
        int r = (rgb >> 16) & 0xFF;
        int g = (rgb >> 8) & 0xFF;
        int b = rgb & 0xFF;
        r = (r * percent) / 100;
        g = (g * percent) / 100;
        b = (b * percent) / 100;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
    }

    private static int darkenRgb(int rgb, int percent) {
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        int r = (rgb >> 16) & 0xFF;
        int g = (rgb >> 8) & 0xFF;
        int b = rgb & 0xFF;
        r = (r * percent) / 100;
        g = (g * percent) / 100;
        b = (b * percent) / 100;
        return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
    }

    private static String[] columnIdsForMode(String mode) {
        if ("skywars".equals(mode)) return SW_COLUMN_IDS;
        if ("duel".equals(mode)) return DUEL_COLUMN_IDS;
        if ("bedwarsduels".equals(mode)) return BWD_COLUMN_IDS;
        if ("murdermystery".equals(mode)) return MM_COLUMN_IDS;
        return BW_COLUMN_IDS;
    }

    private static String[] columnLabelsForMode(String mode) {
        if ("skywars".equals(mode)) return SW_COLUMN_LABELS;
        if ("duel".equals(mode)) return DUEL_COLUMN_LABELS;
        if ("bedwarsduels".equals(mode)) return BWD_COLUMN_LABELS;
        if ("murdermystery".equals(mode)) return MM_COLUMN_LABELS;
        return BW_COLUMN_LABELS;
    }

    private static String normalizeColumnIdForMode(String mode, String id) {
        if ("skywars".equals(mode)) {
            if ("stars".equals(id)) return "sw_stars";
            if ("kdr".equals(id)) return "sw_kdr";
            if ("wlr".equals(id)) return "sw_wlr";
            if ("wins".equals(id)) return "sw_wins";
            if ("kills".equals(id)) return "sw_kills";
        } else if ("duel".equals(mode)) {
            if ("wins".equals(id)) return "duel_wins";
            if ("wlr".equals(id)) return "duel_wlr";
            if ("kdr".equals(id)) return "duel_kdr";
        } else if ("bedwarsduels".equals(mode)) {
            if ("wlr".equals(id)) return "bwd_wlr";
            if ("index".equals(id)) return "bwd_index";
        } else if ("murdermystery".equals(mode)) {
            if ("kdr".equals(id)) return "mm_kdr";
            if ("wlr".equals(id)) return "mm_wlr";
            if ("kills".equals(id)) return "mm_kills";
            if ("wins".equals(id)) return "mm_wins";
            if ("icon".equals(id)) return "mm_icon";
        }
        return id;
    }

    private int[] getOverlaySortedIndexes() {
        int n = columnIdsForMode(overlayGamemodeEdit).length;
        int[] idx = new int[n];
        for (int i = 0; i < n; i++) idx[i] = i;
        for (int i = 0; i < n - 1; i++) {
            for (int j = i + 1; j < n; j++) {
                if (overlayColumnOrder[idx[i]] > overlayColumnOrder[idx[j]]) {
                    int t = idx[i];
                    idx[i] = idx[j];
                    idx[j] = t;
                }
            }
        }
        return idx;
    }

    private int findOverlayIndexByOrder(int targetOrder) {
        for (int i = 0; i < overlayColumnOrder.length; i++) {
            if (overlayColumnOrder[i] == targetOrder) return i;
        }
        return -1;
    }

    private void moveOverlayColumn(int index, int dir) {
        if (index < 0 || index >= columnIdsForMode(overlayGamemodeEdit).length) return;
        int order = overlayColumnOrder[index];
        int other = findOverlayIndexByOrder(order + dir);
        if (other < 0) return;
        int t = overlayColumnOrder[index];
        overlayColumnOrder[index] = overlayColumnOrder[other];
        overlayColumnOrder[other] = t;
    }

    private static String defaultColumnsForMode(String mode) {
        if ("skywars".equals(mode)) return DEFAULT_COLS_SKYWARS;
        if ("duel".equals(mode)) return DEFAULT_COLS_DUEL;
        if ("bedwarsduels".equals(mode)) return DEFAULT_COLS_BWDUELS;
        if ("murdermystery".equals(mode)) return DEFAULT_COLS_MURDER;
        return DEFAULT_COLS_BEDWARS;
    }

    private static String getStoredColumnsForMode(String mode) {
        if ("skywars".equals(mode)) {
            return overlayColumnsSkywars.length() > 0 ? overlayColumnsSkywars : DEFAULT_COLS_SKYWARS;
        }
        if ("duel".equals(mode)) {
            return overlayColumnsDuel.length() > 0 ? overlayColumnsDuel : DEFAULT_COLS_DUEL;
        }
        if ("bedwarsduels".equals(mode)) {
            return overlayColumnsBedwarsduels.length() > 0 ? overlayColumnsBedwarsduels : DEFAULT_COLS_BWDUELS;
        }
        if ("murdermystery".equals(mode)) {
            return overlayColumnsMurdermystery.length() > 0 ? overlayColumnsMurdermystery : DEFAULT_COLS_MURDER;
        }
        return overlayColumnsBedwars.length() > 0 ? overlayColumnsBedwars : DEFAULT_COLS_BEDWARS;
    }

    private static void setStoredColumnsForMode(String mode, String cfg) {
        if (cfg == null) cfg = "";
        if ("skywars".equals(mode)) overlayColumnsSkywars = cfg;
        else if ("duel".equals(mode)) overlayColumnsDuel = cfg;
        else if ("bedwarsduels".equals(mode)) overlayColumnsBedwarsduels = cfg;
        else if ("murdermystery".equals(mode)) overlayColumnsMurdermystery = cfg;
        else overlayColumnsBedwars = cfg;
    }

    private static String buildColumnsConfigFromState() {
        String[] ids = columnIdsForMode(overlayGamemodeEdit);
        int n = ids.length;
        int[] idx = new int[n];
        for (int i = 0; i < n; i++) idx[i] = i;
        for (int i = 0; i < n - 1; i++) {
            for (int j = i + 1; j < n; j++) {
                if (overlayColumnOrder[idx[i]] > overlayColumnOrder[idx[j]]) {
                    int t = idx[i];
                    idx[i] = idx[j];
                    idx[j] = t;
                }
            }
        }

        StringBuilder sb = new StringBuilder();
        sb.append("name,hp");
        for (int i = 0; i < n; i++) {
            int k = idx[i];
            if (overlayColumnEnabled[k]) {
                sb.append(',');
                sb.append(ids[k]);
            }
        }
        return sb.toString();
    }

    public static void snapshotOverlayColumnsEditMode() {
        setStoredColumnsForMode(overlayGamemodeEdit, buildColumnsConfigFromState());
    }

    public static void loadOverlayColumnsForEditMode() {
        setOverlayColumnsFromConfig(getStoredColumnsForMode(overlayGamemodeEdit));
    }

    public static void setOverlayGamemodeEdit(String mode) {
        if (mode == null || mode.length() == 0) return;
        snapshotOverlayColumnsEditMode();
        overlayGamemodeEdit = mode;
        loadOverlayColumnsForEditMode();
    }

    public static void setOverlayGamemodeActive(String mode) {
        if (mode == null || mode.length() == 0) return;
        overlayGamemodeActive = mode;
    }

    public static String getOverlayColumnsConfig() {
        String mode = overlayGamemodeActive;
        if (mode == null || mode.length() == 0 || "unknown".equals(mode)) mode = "bedwars";
        return getStoredColumnsForMode(mode);
    }

    public static String getOverlayColumnsBedwars() {
        snapshotOverlayColumnsEditMode();
        return getStoredColumnsForMode("bedwars");
    }

    public static String getOverlayColumnsSkywars() {
        snapshotOverlayColumnsEditMode();
        return getStoredColumnsForMode("skywars");
    }

    public static String getOverlayColumnsDuel() {
        snapshotOverlayColumnsEditMode();
        return getStoredColumnsForMode("duel");
    }

    public static String getOverlayColumnsBedwarsduels() {
        snapshotOverlayColumnsEditMode();
        return getStoredColumnsForMode("bedwarsduels");
    }

    public static String getOverlayColumnsMurdermystery() {
        snapshotOverlayColumnsEditMode();
        return getStoredColumnsForMode("murdermystery");
    }

    public static void loadAllOverlayColumnsFromToggles(String bedwars, String skywars, String duel,
                                                        String bedwarsduels, String murdermystery, String legacy) {
        if (bedwars != null && bedwars.length() > 0) overlayColumnsBedwars = bedwars;
        else if (legacy != null && legacy.length() > 0) overlayColumnsBedwars = legacy;
        if (skywars != null && skywars.length() > 0) overlayColumnsSkywars = skywars;
        if (duel != null && duel.length() > 0) overlayColumnsDuel = duel;
        if (bedwarsduels != null && bedwarsduels.length() > 0) overlayColumnsBedwarsduels = bedwarsduels;
        if (murdermystery != null && murdermystery.length() > 0) overlayColumnsMurdermystery = murdermystery;
        loadOverlayColumnsForEditMode();
    }

    public static void setOverlayColumnsFromConfig(String cfg) {
        String[] ids = columnIdsForMode(overlayGamemodeEdit);
        int n = ids.length;
        if (cfg == null) cfg = "";

        for (int i = 0; i < MAX_OVERLAY_COLUMNS; i++) {
            overlayColumnEnabled[i] = false;
            overlayColumnOrder[i] = MAX_OVERLAY_COLUMNS + i;
        }

        int order = 0;
        String[] parts = cfg.split(",");
        for (int i = 0; i < parts.length; i++) {
            String id = normalizeColumnIdForMode(overlayGamemodeEdit, parts[i].trim());
            if (id.length() == 0 || "name".equals(id) || "hp".equals(id) || "tags".equals(id)) continue;
            if ("magicRatio".equals(id)) id = "requeuePct";
            for (int j = 0; j < n; j++) {
                if (ids[j].equals(id)) {
                    overlayColumnEnabled[j] = true;
                    overlayColumnOrder[j] = order++;
                    break;
                }
            }
        }

        for (int i = 0; i < n; i++) {
            if (!overlayColumnEnabled[i]) {
                overlayColumnOrder[i] = order++;
            }
        }
    }

    // Layout constants to keep content inside the panel bounds.
    private static final int HEADER_H = 25;
    private static final int FOOTER_H = 12;
    private static final int SIDEBAR_W = 120;
    private static final int PANEL_INSET = 10;
    private static final int CONTENT_GAP = 14;
    private static final int SCROLLBAR_W = 4;
    private static final int SCROLLBAR_GAP = 8;
    private static final int CONTENT_INNER_GAP = 8;
    private static final int OVERLAY_FIXED_HEADER_H = 104;

    private static final int CLR_ON = 0xFF75D6B0;
    private static final int CLR_OFF = 0xFFE58B86;
    private static final int CLR_CHOICE = 0xFF8DBDE8;
    private static final int CLR_ACTION = 0xFFE4B875;
    private static final int CLR_MUTED = 0xFF65726D;
    private static final int CLR_ACCENT = 0xFF9AE4C5;
    private static final int CLR_TEXT_PRIMARY = 0xFFE7EEEA;
    private static final int CLR_TEXT_MUTED = 0xFFA1AEA8;
    private static final int CLR_FRAME = 0xEE151B19;
    private static final int CLR_SIDEBAR = 0xFF111715;
    private static final int CLR_CONTENT = 0xFF151C19;
    private static final int CLR_HEADER = 0xFF101613;
    private static final int CLR_FOOTER = 0xFF101613;
    private static final int CLR_BORDER = 0xFF2D3A34;
    private static final int CLR_CARD = 0xFF19211E;
    private static final int CLR_CARD_EDGE = 0xFF293630;
    private static final int CLR_CARD_BOTTOM = 0xFF111715;
    private static final int CLR_ROW = 0xFF171E1B;
    private static final int CLR_ROW_HOVER = 0xFF202A25;
    private static final int CLR_ROW_SELECTED = 0xFF22332C;
    private static final int CLR_FIELD = 0xFF101613;

    private FontRenderer resolveFontRenderer() {
        if (cachedFontRenderer != null) return cachedFontRenderer;
        try {
            Minecraft mc = Minecraft.getMinecraft();
            if (mc != null && mc.fontRendererObj != null) {
                cachedFontRenderer = mc.fontRendererObj;
                return cachedFontRenderer;
            }
        } catch (Throwable ignored) {
        }
        return null;
    }

    private void drawTextLeft(String text, int x, int y, int color) {
        FontRenderer fr = resolveFontRenderer();
        if (fr != null) this.drawString(fr, text, x, y, color);
    }

    private void drawTextCentered(String text, int x, int y, int color) {
        FontRenderer fr = resolveFontRenderer();
        if (fr != null) this.drawCenteredString(fr, text, x, y, color);
    }

    private int getPanelScroll(int theme) {
        if (theme == 0) return settingsScroll;
        if (theme == 1) return 0;
        if (theme == 2) return utilsScroll;
        if (theme == 3) return overlayScroll;
        if (theme == 4) return chatOverlayScroll;
        if (theme == 5) return sessionStatsScroll;
        if (theme == 6) return experimentalScroll;
        if (theme == 7) return blackchifyScroll;
        return 0;
    }

    private void setPanelScroll(int theme, int value) {
        if (theme == 0) settingsScroll = value;
        else if (theme == 1) anticheatScroll = 0;
        else if (theme == 2) {
            utilsScroll = clampScroll(value, gameplayPanelContentHeight(),
                winH - HEADER_H - FOOTER_H - (PANEL_INSET * 2));
        }
        else if (theme == 3) overlayScroll = value;
        else if (theme == 4) chatOverlayScroll = value;
        else if (theme == 5) sessionStatsScroll = value;
        else if (theme == 6) experimentalScroll = value;
        else if (theme == 7) blackchifyScroll = value;
    }

    private int getPanelContentHeight(int theme) {
        if (theme == 0) return 220;
        if (theme == 1) return 192;
        if (theme == 2) return gameplayPanelContentHeight();
        if (theme == 3) return OVERLAY_FIXED_HEADER_H + 24 + (columnIdsForMode(overlayGamemodeEdit).length * 24);
        if (theme == 4) return 220;
        if (theme == 5) return sessionStatsPanelContentHeight();
        if (theme == 6) return 72;
        if (theme == 7) return blackchifyPanelContentHeight();
        return 0;
    }

    private static int blackchifyPanelContentHeight() {
        return 4 + 40 + 12 + 50 + (QUICKBUY_IDS.length * 26) + 8;
    }

    private static int sessionStatsPanelContentHeight() {
        return SESSION_DISPLAY_SECTION_Y + SESSION_DISPLAY_CARD_H + 8;
    }

    private int clampScroll(int scroll, int contentHeight, int viewportHeight) {
        int minScroll = Math.min(0, viewportHeight - contentHeight);
        if (scroll > 0) scroll = 0;
        if (scroll < minScroll) scroll = minScroll;
        return scroll;
    }

    private boolean isHoveredRect(int x, int y, int w, int h, int mouseX, int mouseY) {
        return mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
    }

    private boolean isRowVisible(int y, int h, int top, int bottom) {
        return y + h > top && y < bottom;
    }

    private void drawSectionCardClipped(int x, int y, int w, int h, int viewportTop, int viewportBottom) {
        if (!isRowVisible(y, h, viewportTop, viewportBottom)) return;
        int y0 = Math.max(y, viewportTop);
        int y1 = Math.min(y + h, viewportBottom);
        Gui.drawRect(x, y0, x + w, y1, CLR_CARD);
        if (y >= viewportTop) Gui.drawRect(x, y, x + w, y + 1, CLR_CARD_EDGE);
        if (y + h <= viewportBottom) Gui.drawRect(x, y + h - 1, x + w, y + h, CLR_CARD_BOTTOM);
    }

    private void drawContentViewportMask(int contentX, int maskRight, int viewportTop, int viewportBottom,
                                         int panelBottom, int bg) {
        int topMaskStart = winY;
        if (topMaskStart < viewportTop) {
            Gui.drawRect(contentX - 4, topMaskStart, maskRight, viewportTop, bg);
        }
        if (viewportBottom < panelBottom) {
            Gui.drawRect(contentX - 4, viewportBottom, maskRight, panelBottom, bg);
        }
    }

    private static int gameplayPanelContentHeight() {
        return UTILS_GL_FIELD_Y + 34;
    }

    private void drawScrollBar(int x, int y, int h, int contentHeight, int scroll) {
        if (contentHeight <= h) return;
        int track = CLR_FIELD;
        int thumb = CLR_MUTED;
        Gui.drawRect(x, y, x + SCROLLBAR_W, y + h, track);
        int thumbH = Math.max(14, (h * h) / contentHeight);
        int range = h - thumbH;
        int maxScroll = contentHeight - h;
        int offset = 0;
        if (maxScroll > 0) {
            offset = (int)(((long)(-scroll) * (long)range) / (long)maxScroll);
        }
        Gui.drawRect(x, y + offset, x + SCROLLBAR_W, y + offset + thumbH, thumb);
    }

    private void drawThemeItem(int x, int y, int w, int h, String label, boolean selected, int mouseX, int mouseY) {
        int bg = selected ? CLR_ROW_SELECTED : CLR_SIDEBAR;
        if (isHoveredRect(x, y, w, h, mouseX, mouseY)) bg = selected ? 0xFF294036 : CLR_ROW_HOVER;
        Gui.drawRect(x, y, x + w, y + h, bg);
        if (selected) {
            Gui.drawRect(x + 2, y + 2, x + 4, y + h - 2, CLR_ON);
        }
        drawTextLeft(label, x + 10, y + 8, selected ? CLR_TEXT_PRIMARY : CLR_TEXT_MUTED);
    }

    private String maskValue(String value) {
        if (value == null || value.length() == 0) return "";
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < value.length(); i++) sb.append('*');
        return sb.toString();
    }

    private void drawInputField(int x, int y, int w, String label, String value, boolean showValue, boolean active, int mouseX, int mouseY) {
        drawTextLeft(label, x, y - 10, 0xFFCFCFCF);
        int bg = active ? 0xFF1D2A24 : CLR_FIELD;
        if (isHoveredRect(x, y, w, 20, mouseX, mouseY)) bg = active ? 0xFF24352D : CLR_ROW_HOVER;
        Gui.drawRect(x, y, x + w, y + 20, bg);
        Gui.drawRect(x, y, x + 1, y + 20, active ? CLR_ON : CLR_BORDER);

        String shown = showValue ? value : maskValue(value);
        if (shown == null) shown = "";
        if (shown.length() > 48) shown = shown.substring(0, 48);
        if (active && ((System.currentTimeMillis() / 400L) % 2L) == 0L) shown += "_";
        drawTextLeft(shown, x + 6, y + 6, CLR_TEXT_PRIMARY);
    }

    private void drawEyeButton(int x, int y, boolean show, int mouseX, int mouseY) {
        int bg = isHoveredRect(x, y, 20, 20, mouseX, mouseY) ? CLR_ROW_HOVER : CLR_CARD;
        Gui.drawRect(x, y, x + 20, y + 20, bg);
        drawTextLeft(show ? "O" : "o", x + 7, y + 6, CLR_TEXT_PRIMARY);
    }

    private void drawClearButton(int x, int y, int mouseX, int mouseY) {
        int bg = isHoveredRect(x, y, 20, 20, mouseX, mouseY) ? 0xFF382321 : CLR_CARD;
        Gui.drawRect(x, y, x + 20, y + 20, bg);
        drawTextLeft("x", x + 7, y + 6, 0xFFFF5555);
    }

    private void closeScreenSafe() {
        try {
            Minecraft mc = Minecraft.getMinecraft();
            if (mc != null) mc.displayGuiScreen(null);
        } catch (Throwable ignored) {
        }
    }

    private void playUiClickSound() {
        try {
            Object mc = this.mc;
            Class mcCls = null;
            if (mc != null) {
                mcCls = mc.getClass();
            } else {
                mcCls = Class.forName("ave");
                Method getMc = mcCls.getMethod("A");
                mc = getMc.invoke(null);
            }
            if (mc == null) return;
            Object soundHandler = null;
            Method[] mcMethods = mcCls.getDeclaredMethods();
            for (int i = 0; i < mcMethods.length; i++) {
                Method m = mcMethods[i];
                if (m.getParameterTypes().length == 0 && m.getReturnType() != Void.TYPE) {
                    String n = m.getName();
                    if ("P".equals(n) || "getSoundHandler".equals(n) || m.getReturnType().getName().indexOf("SoundHandler") >= 0 || "bqf".equals(m.getReturnType().getName())) {
                        m.setAccessible(true);
                        Object v = m.invoke(mc);
                        if (v != null) {
                            soundHandler = v;
                            break;
                        }
                    }
                }
            }

            Field[] fields = mcCls.getDeclaredFields();
            if (soundHandler == null) {
                for (int i = 0; i < fields.length; i++) {
                    Field f = fields[i];
                    if (f.getType() != null && (f.getType().getName().indexOf("SoundHandler") >= 0 || "bqf".equals(f.getType().getName()))) {
                        f.setAccessible(true);
                        soundHandler = f.get(mc);
                        if (soundHandler != null) break;
                    }
                }
            }
            if (soundHandler == null) return;

            // Preferred path: call vanilla button playPressSound(soundHandler), which guarantees the native UI click sound.
            try {
                Class buttonCls = null;
                try { buttonCls = Class.forName("avs"); } catch (Throwable ignored) {}
                if (buttonCls == null) {
                    try { buttonCls = Class.forName("net.minecraft.client.gui.GuiButton"); } catch (Throwable ignored) {}
                }

                if (buttonCls != null) {
                    Object dummyBtn = null;
                    Constructor[] btnCtors = buttonCls.getConstructors();
                    for (int i = 0; i < btnCtors.length; i++) {
                        Constructor c = btnCtors[i];
                        Class[] pt = c.getParameterTypes();
                        Object[] args = new Object[pt.length];
                        boolean ok = true;
                        for (int j = 0; j < pt.length; j++) {
                            if (pt[j] == Integer.TYPE || pt[j] == Integer.class) args[j] = Integer.valueOf(0);
                            else if (pt[j] == Boolean.TYPE || pt[j] == Boolean.class) args[j] = Boolean.TRUE;
                            else if (pt[j] == String.class) args[j] = "";
                            else {
                                ok = false;
                                break;
                            }
                        }
                        if (ok) {
                            dummyBtn = c.newInstance(args);
                            break;
                        }
                    }

                    if (dummyBtn != null) {
                        Method[] btnMethods = buttonCls.getDeclaredMethods();
                        for (int i = 0; i < btnMethods.length; i++) {
                            Method m = btnMethods[i];
                            Class[] pt = m.getParameterTypes();
                            if (pt.length == 1 && pt[0].isInstance(soundHandler) && m.getReturnType() == Void.TYPE) {
                                m.setAccessible(true);
                                m.invoke(dummyBtn, soundHandler);
                                return;
                            }
                        }
                    }
                }
            } catch (Throwable ignored) {
            }

            // Fallback path: build and play gui.button.press sound via sound API reflection.
            Class resLocCls = null;
            Class posSoundCls = null;
            try { resLocCls = Class.forName("jy"); } catch (Throwable ignored) {}
            try { posSoundCls = Class.forName("bqh"); } catch (Throwable ignored) {}
            if (resLocCls == null || posSoundCls == null) {
                try { resLocCls = Class.forName("net.minecraft.util.ResourceLocation"); } catch (Throwable ignored) {}
                try { posSoundCls = Class.forName("net.minecraft.client.audio.PositionedSoundRecord"); } catch (Throwable ignored) {}
            }
            if (resLocCls == null || posSoundCls == null) return;

            Constructor[] ctors = resLocCls.getConstructors();
            Object resLoc = null;
            for (int i = 0; i < ctors.length; i++) {
                Constructor c = ctors[i];
                Class[] pt = c.getParameterTypes();
                if (pt.length == 1 && pt[0] == String.class) {
                    resLoc = c.newInstance("gui.button.press");
                    break;
                }
                if (pt.length == 2 && pt[0] == String.class && pt[1] == String.class) {
                    resLoc = c.newInstance("gui", "button.press");
                    break;
                }
            }
            if (resLoc == null) return;

            Object sound = null;
            Method[] posMethods = posSoundCls.getDeclaredMethods();
            for (int i = 0; i < posMethods.length; i++) {
                Method m = posMethods[i];
                if (!Modifier.isStatic(m.getModifiers())) continue;
                Class[] pt = m.getParameterTypes();
                if (pt.length == 2 && pt[0] == resLocCls && (pt[1] == float.class || pt[1] == Float.class)
                        && posSoundCls.isAssignableFrom(m.getReturnType())) {
                    m.setAccessible(true);
                    sound = m.invoke(null, resLoc, new Float(1.0F));
                    break;
                }
                if (pt.length == 1 && pt[0] == resLocCls && posSoundCls.isAssignableFrom(m.getReturnType())) {
                    m.setAccessible(true);
                    sound = m.invoke(null, resLoc);
                    break;
                }
            }
            if (sound == null) return;

            Method[] shMethods = soundHandler.getClass().getDeclaredMethods();
            for (int i = 0; i < shMethods.length; i++) {
                Method m = shMethods[i];
                Class[] pt = m.getParameterTypes();
                if (pt.length == 1 && pt[0].isInstance(sound)) {
                    m.setAccessible(true);
                    m.invoke(soundHandler, sound);
                    return;
                }
            }
        } catch (Throwable ignored) {
        }
    }

    private void drawToggle(int x, int y, int w, String name, boolean state, boolean enabled, int mouseX, int mouseY) {
        drawCardToggleRow(x, y, w, name, state, enabled, mouseX, mouseY);
    }

    private void drawCardToggleRow(int x, int y, int w, String name, boolean state, boolean enabled,
                                   int mouseX, int mouseY) {
        final int h = 22;
        int bg = enabled ? (state ? 0xFF19251F : 0xFF211B1A) : 0xFF131715;
        if (enabled && isHoveredRect(x, y, w, h, mouseX, mouseY)) bg = state ? 0xFF22352B : 0xFF302220;
        Gui.drawRect(x, y, x + w, y + h, bg);
        int stripe = enabled ? (state ? CLR_ON : CLR_OFF) : CLR_MUTED;
        Gui.drawRect(x, y, x + 2, y + h, stripe);
        int text = enabled ? (state ? CLR_TEXT_PRIMARY : CLR_TEXT_MUTED) : CLR_MUTED;
        drawTextLeft(name, x + 8, y + 7, text);
        drawMiniSwitch(x + w - 34, y + 5, 28, 12, state, enabled, mouseX, mouseY);
    }

    private void drawPanelSectionTitle(int x, int y, String title, int accent, int viewportTop, int viewportBottom) {
        if (isRowVisible(y, UTILS_TITLE_H + 2, viewportTop, viewportBottom)) {
            drawTextLeft(title, x, y, accent);
        }
    }

    private void drawPanelDivider(int x, int y, int w, int viewportTop, int viewportBottom) {
        if (isRowVisible(y, 2, viewportTop, viewportBottom)) {
            Gui.drawRect(x, y, x + w, y + 2, CLR_BORDER);
        }
    }

    private boolean hitCardSwitch(int cardX, int cardW, int rowY, int panelTop, int panelBottom, int mouseX, int mouseY) {
        return isRowVisible(rowY, 22, panelTop, panelBottom)
                && isHoveredRect(cardX + cardW - 34, rowY + 5, 28, 12, mouseX, mouseY);
    }

    private boolean hitChip(int x, int y, int w, int h, int panelTop, int panelBottom, int mouseX, int mouseY) {
        return isRowVisible(y, h, panelTop, panelBottom) && isHoveredRect(x, y, w, h, mouseX, mouseY);
    }

    private static final int UTILS_FEATURES_Y = 4;
    private static final int UTILS_FEATURES_H = 282;
    private static final int UTILS_HITBOX_SLIDER_Y = 90;
    private static final int UTILS_HITBOX_SLIDER_H = 16;
    private static final int UTILS_HITBOX_SLIDER_KNOB = 10;
    private static final int UTILS_SHOT_DISTANCE_Y = 122;
    private static final int UTILS_BED_DC_Y = 154;
    private static final int UTILS_FIREBALL_Y = 186;
    private static final int UTILS_FIREBALL_COLOR_Y = 218;
    private static final int UTILS_NICK_QOL_Y = 250;
    private static final char[] MC_COLOR_CODES = {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };
    private static final int[] MC_COLOR_RGB = {
        0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
        0xFFAA0000, 0xFFAA00AA, 0xFFFFAA00, 0xFFAAAAAA,
        0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
        0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
    };
    private static final int UTILS_SECTION_GAP = 12;
    private static final int UTILS_TITLE_H = 12;
    private static final int UTILS_HOTKEYS_TITLE_Y = UTILS_FEATURES_Y + UTILS_FEATURES_H + UTILS_SECTION_GAP;
    private static final int UTILS_HOTKEYS_SECTION_Y = UTILS_HOTKEYS_TITLE_Y + UTILS_TITLE_H + 4;
    private static final int UTILS_HOTKEYS_CARD_H = 8 + (12 * 26) + 8;
    private static final int RESOURCE_TIMER_TITLE_Y = UTILS_HOTKEYS_SECTION_Y + UTILS_HOTKEYS_CARD_H + UTILS_SECTION_GAP;
    private static final int RESOURCE_TIMER_SECTION_Y = RESOURCE_TIMER_TITLE_Y + UTILS_TITLE_H + 4;
    private static final int RESOURCE_TIMER_CARD_H = 96;
    private static final int UTILS_GL_TITLE_Y = RESOURCE_TIMER_SECTION_Y + RESOURCE_TIMER_CARD_H + UTILS_SECTION_GAP;
    private static final int UTILS_GL_FIELD_Y = UTILS_GL_TITLE_Y + UTILS_TITLE_H + 4;

    private static final int AC_MAIN_Y = 4;
    private static final int AC_MAIN_H = 50;
    private static final int AC_CHECKS_Y = 68;
    private static final int AC_CHECKS_H = 116;

    private static final int CHAT_MAIN_Y = 4;
    private static final int CHAT_MAIN_H = 48;
    private static final int CHAT_THRESH_Y = 68;
    private static final int CHAT_THRESH_H = 144;
    private static final int SESSION_STATS_Y = 4;
    private static final int SESSION_STATS_H = 140;
    private static final int SESSION_DISPLAY_TITLE_Y = SESSION_STATS_Y + SESSION_STATS_H + UTILS_SECTION_GAP;
    private static final int SESSION_DISPLAY_SECTION_Y = SESSION_DISPLAY_TITLE_Y + UTILS_TITLE_H + 4;
    private static final int SESSION_DISPLAY_CARD_H = 176;

    private void drawSectionCard(int x, int y, int w, int h) {
        Gui.drawRect(x, y, x + w, y + h, CLR_CARD);
        Gui.drawRect(x, y, x + w, y + 1, CLR_CARD_EDGE);
        Gui.drawRect(x, y + h - 1, x + w, y + h, CLR_CARD_BOTTOM);
    }

    private void drawMiniSwitch(int x, int y, int w, int h, boolean on, boolean enabled, int mouseX, int mouseY) {
        int track = enabled ? (on ? 0xFF29483A : 0xFF482B29) : 0xFF202724;
        if (enabled && isHoveredRect(x, y, w, h, mouseX, mouseY)) track = on ? 0xFF35614C : 0xFF5B3531;
        Gui.drawRect(x, y, x + w, y + h, track);
        int knobW = h - 2;
        int knobX = on ? (x + w - knobW - 1) : (x + 1);
        int knob = enabled ? (on ? CLR_ON : CLR_OFF) : CLR_MUTED;
        Gui.drawRect(knobX, y + 1, knobX + knobW, y + h - 1, knob);
    }

    private static String holdRdKeyLabel() {
        if (holdRdKeyCode <= 0) return "NONE";
        try {
            String name = Keyboard.getKeyName(holdRdKeyCode);
            if (name != null && name.length() > 0) return name;
        } catch (Throwable ignored) {
        }
        return "#" + holdRdKeyCode;
    }

    private static String nickQolKeyLabel() {
        if (hkNickQol <= 0) return "NONE";
        try {
            String name = Keyboard.getKeyName(hkNickQol);
            if (name != null && name.length() > 0) return name;
        } catch (Throwable ignored) {
        }
        return "#" + hkNickQol;
    }

    private void drawChipToggle(int x, int y, int w, int h, String label,
                                boolean state, boolean enabled, int mouseX, int mouseY) {
        drawChipToggle(x, y, w, h, label, state, enabled, mouseX, mouseY, true);
    }

    private void drawChipToggle(int x, int y, int w, int h, String label,
                                boolean state, boolean enabled, int mouseX, int mouseY, boolean showOnOff) {
        int bg = enabled ? (state ? 0xFF19251F : 0xFF211B1A) : 0xFF151A18;
        if (enabled && isHoveredRect(x, y, w, h, mouseX, mouseY)) bg = state ? 0xFF22352B : 0xFF302220;
        Gui.drawRect(x, y, x + w, y + h, bg);
        int stripe = enabled ? (state ? CLR_ON : CLR_OFF) : CLR_MUTED;
        Gui.drawRect(x, y, x + 2, y + h, stripe);
        int text = enabled ? (state ? CLR_ON : CLR_OFF) : 0xFF585858;
        if (!showOnOff) {
            // Color-only toggle (e.g. Turbo): label uses state color, no ON/OFF text.
            text = enabled ? (state ? CLR_ON : CLR_OFF) : 0xFF585858;
        } else {
            text = enabled ? (state ? CLR_TEXT_PRIMARY : CLR_TEXT_MUTED) : CLR_MUTED;
        }
        drawTextLeft(label, x + 8, y + 7, text);
        if (enabled && showOnOff) {
            String status = state ? "ON" : "OFF";
            drawTextLeft(status, x + w - 26, y + 7, state ? CLR_ON : CLR_OFF);
        }
    }

    private void drawValueChip(int x, int y, int w, int h, String label, int mouseX, int mouseY) {
        int bg = isHoveredRect(x, y, w, h, mouseX, mouseY) ? 0xFF202B32 : 0xFF182127;
        Gui.drawRect(x, y, x + w, y + h, bg);
        Gui.drawRect(x, y, x + 2, y + h, CLR_CHOICE);
        drawTextLeft(label, x + 8, y + 7, CLR_CHOICE);
    }

    private void drawActionChip(int x, int y, int w, int h, String label, int mouseX, int mouseY) {
        int bg = isHoveredRect(x, y, w, h, mouseX, mouseY) ? 0xFF352C1B : 0xFF211D15;
        Gui.drawRect(x, y, x + w, y + h, bg);
        Gui.drawRect(x, y, x + 2, y + h, CLR_ACTION);
        drawTextLeft(label, x + 7, y + 7, CLR_ACTION);
    }

    private int hitboxSliderTrackX(int contentX) {
        return contentX + 110;
    }

    private int hitboxSliderTrackW(int contentW) {
        return Math.max(40, contentW - 148);
    }

    private int hitboxThicknessFromMouse(int trackX, int trackW, int mouseX) {
        int knob = UTILS_HITBOX_SLIDER_KNOB;
        int usable = Math.max(1, trackW - knob);
        int rel = mouseX - trackX - (knob / 2);
        if (rel < 0) rel = 0;
        if (rel > usable) rel = usable;
        float t = (float)rel / (float)usable;
        return normalizeHitboxThickness(1 + Math.round(t * 9.0f));
    }

    private void drawHorizontalSlider(int x, int y, int w, int h, int min, int max, int value,
                                      boolean enabled, boolean dragging, int mouseX, int mouseY) {
        int trackY = y + (h / 2) - 1;
        int track = enabled ? 0xFF43524B : 0xFF29302D;
        if (enabled && (dragging || isHoveredRect(x, y, w, h, mouseX, mouseY))) track = 0xFF61736A;
        Gui.drawRect(x, trackY, x + w, trackY + 2, track);

        float t = (max > min) ? (float)(value - min) / (float)(max - min) : 0.0f;
        int knob = Math.min(UTILS_HITBOX_SLIDER_KNOB, Math.max(6, h - 4));
        int knobY = y + (h - knob) / 2;
        int knobCenter = x + (knob / 2) + (int)(t * Math.max(1, w - knob));
        int knobColor = enabled ? CLR_CHOICE : CLR_MUTED;
        Gui.drawRect(knobCenter - (knob / 2), knobY, knobCenter + (knob / 2), knobY + knob, knobColor);
    }

    private void drawSegmentedChoice(int x, int y, int w, int h, String[] labels, int selectedIndex,
                                     boolean enabled, int mouseX, int mouseY) {
        int gap = 2;
        int segW = (w - (gap * (labels.length - 1))) / labels.length;
        for (int i = 0; i < labels.length; i++) {
            int sx = x + (i * (segW + gap));
            boolean selected = i == selectedIndex;
            int bg = enabled ? (selected ? 0xFF1D3028 : CLR_FIELD) : 0xFF131715;
            if (enabled && isHoveredRect(sx, y, segW, h, mouseX, mouseY)) {
                bg = selected ? 0xFF294036 : CLR_ROW_HOVER;
            }
            Gui.drawRect(sx, y, sx + segW, y + h, bg);
            if (selected && enabled) Gui.drawRect(sx + 2, y + 2, sx + 4, y + h - 2, CLR_CHOICE);
            int color = enabled ? (selected ? CLR_ON : CLR_TEXT_MUTED) : CLR_MUTED;
            drawTextLeft(labels[i], sx + 6, y + 6, color);
        }
    }

    private void drawResourceTimerPanel(int contentX, int cy, int contentW, int viewportTop, int viewportBottom,
                                        int mouseX, int mouseY, int cfgTextAccent, int cfgTextMuted) {
        int titleY = cy + RESOURCE_TIMER_TITLE_Y;
        drawPanelSectionTitle(contentX, titleY, "Resource Timer", cfgTextAccent, viewportTop, viewportBottom);

        int cardY = cy + RESOURCE_TIMER_SECTION_Y;
        if (!isRowVisible(cardY, RESOURCE_TIMER_CARD_H, viewportTop, viewportBottom)) return;

        drawSectionCardClipped(contentX, cardY, contentW, RESOURCE_TIMER_CARD_H, viewportTop, viewportBottom);

        if (isRowVisible(cardY + 6, 18, viewportTop, viewportBottom)) {
            drawTextLeft("Enabled", contentX + 8, cardY + 7, overlayResourceHeader ? CLR_ON : CLR_OFF);
            drawMiniSwitch(contentX + contentW - 34, cardY + 6, 28, 12, overlayResourceHeader, true, mouseX, mouseY);
        }

        int chipY = cardY + 24;
        int chipH = 22;
        int chipW = (contentW - 12) / 2;
        boolean subEnabled = overlayResourceHeader;
        if (isRowVisible(chipY, chipH, viewportTop, viewportBottom)) {
            drawChipToggle(contentX + 4, chipY, chipW, chipH, "Diamonds", overlayResourceDiamonds, subEnabled, mouseX, mouseY);
            drawChipToggle(contentX + 8 + chipW, chipY, chipW, chipH, "Emeralds", overlayResourceEmeralds, subEnabled, mouseX, mouseY);
        }

        if (isRowVisible(cardY + 50, 12, viewportTop, viewportBottom)) {
            drawTextLeft("Display", contentX + 8, cardY + 50, cfgTextMuted);
        }
        int segY = cardY + 62;
        int segH = 20;
        if (isRowVisible(segY, segH, viewportTop, viewportBottom)) {
            boolean tabLoc = overlayResourceLocation == null || !"scoreboard".equals(overlayResourceLocation);
            drawSegmentedChoice(contentX + 4, segY, contentW - 8, segH,
                new String[] { "Tab", "Scoreboard" }, tabLoc ? 0 : 1, subEnabled, mouseX, mouseY);
        }
    }

    private boolean handleResourceTimerPanelClicks(int contentX, int cy, int contentW,
                                                     int panelTop, int panelBottom, int mouseX, int mouseY) {
        int cardY = cy + RESOURCE_TIMER_SECTION_Y;
        int cardH = RESOURCE_TIMER_CARD_H;

        if (isRowVisible(cardY, 18, panelTop, panelBottom)
                && isHoveredRect(contentX + contentW - 34, cardY + 6, 28, 12, mouseX, mouseY)) {
            overlayResourceHeader = !overlayResourceHeader;
            return true;
        }
        if (!overlayResourceHeader) return false;

        int chipY = cardY + 24;
        int chipH = 22;
        int chipW = (contentW - 12) / 2;
        if (isRowVisible(chipY, chipH, panelTop, panelBottom)
                && isHoveredRect(contentX + 4, chipY, chipW, chipH, mouseX, mouseY)) {
            overlayResourceDiamonds = !overlayResourceDiamonds;
            return true;
        }
        if (isRowVisible(chipY, chipH, panelTop, panelBottom)
                && isHoveredRect(contentX + 8 + chipW, chipY, chipW, chipH, mouseX, mouseY)) {
            overlayResourceEmeralds = !overlayResourceEmeralds;
            return true;
        }

        int segY = cardY + 62;
        int segH = 20;
        int segW = (contentW - 8 - 2) / 2;
        if (isRowVisible(segY, segH, panelTop, panelBottom)
                && isHoveredRect(contentX + 4, segY, segW, segH, mouseX, mouseY)) {
            overlayResourceLocation = "tab";
            return true;
        }
        if (isRowVisible(segY, segH, panelTop, panelBottom)
                && isHoveredRect(contentX + 4 + segW + 2, segY, segW, segH, mouseX, mouseY)) {
            overlayResourceLocation = "scoreboard";
            return true;
        }
        return false;
    }

    private void drawUtilsPanels(int contentX, int cy, int contentW, int viewportTop, int viewportBottom,
                                 int mouseX, int mouseY, int cfgTextAccent, int cfgTextMuted) {
        int featuresY = cy + UTILS_FEATURES_Y;
        if (isRowVisible(featuresY, UTILS_FEATURES_H, viewportTop, viewportBottom)) {
            drawSectionCardClipped(contentX, featuresY, contentW, UTILS_FEATURES_H, viewportTop, viewportBottom);
            int chipH = 22;
            int chipW = (contentW - 12) / 2;
            int row1 = featuresY + 8;
            int row2 = featuresY + 36;
            int row3 = featuresY + 64;
            int row4 = featuresY + UTILS_SHOT_DISTANCE_Y;
            int row5 = featuresY + UTILS_BED_DC_Y;
            if (isRowVisible(row1, chipH, viewportTop, viewportBottom)) {
                drawChipToggle(contentX + 4, row1, chipW, chipH, "AutoWho", autoWho, true, mouseX, mouseY);
                drawChipToggle(contentX + 8 + chipW, row1, chipW, chipH, "Party Detector", partyDetector, true, mouseX, mouseY);
            }
            if (isRowVisible(row2, chipH, viewportTop, viewportBottom)) {
                drawChipToggle(contentX + 4, row2, chipW, chipH, "Number Denicker", numberDenicker, true, mouseX, mouseY);
                drawChipToggle(contentX + 8 + chipW, row2, chipW, chipH, "Auto GL", autoGL, true, mouseX, mouseY);
            }
            if (isRowVisible(row3, chipH, viewportTop, viewportBottom)) {
                drawChipToggle(contentX + 4, row3, chipW, chipH, "Fast Buy", fastBuy, true, mouseX, mouseY);
                drawChipToggle(contentX + 8 + chipW, row3, chipW, chipH, "Colored Hitboxes", coloredHitboxes, true, mouseX, mouseY);
            }
            int sliderY = featuresY + UTILS_HITBOX_SLIDER_Y;
            int sliderH = UTILS_HITBOX_SLIDER_H;
            if (isRowVisible(sliderY, sliderH, viewportTop, viewportBottom)) {
                int trackX = hitboxSliderTrackX(contentX);
                int trackW = hitboxSliderTrackW(contentW);
                int labelColor = coloredHitboxes ? cfgTextMuted : 0xFF585858;
                drawTextLeft("Thickness", contentX + 8, sliderY + 5, labelColor);
                drawHorizontalSlider(trackX, sliderY, trackW, sliderH, 1, 10, hitboxThickness,
                    coloredHitboxes, draggingHitboxSlider, mouseX, mouseY);
                drawTextLeft(normalizeHitboxThickness(hitboxThickness) + "px",
                    contentX + contentW - 34, sliderY + 5, coloredHitboxes ? CLR_CHOICE : 0xFF585858);
            }
            if (isRowVisible(row4, chipH, viewportTop, viewportBottom)) {
                drawChipToggle(contentX + 4, row4, chipW, chipH, "Dragon Hitbox", dragonHitboxes, true, mouseX, mouseY);
                drawChipToggle(contentX + 8 + chipW, row4, chipW, chipH, "Shot Distance", arrowDistance, true, mouseX, mouseY);
            }
            if (isRowVisible(row5, chipH, viewportTop, viewportBottom)) {
                drawChipToggle(contentX + 4, row5, chipW, chipH, "Bed DC Timer", bedDisconnectTimer, true, mouseX, mouseY);
                drawChipToggle(contentX + 8 + chipW, row5, chipW, chipH, "Trade Indicator", tradeIndicator, true, mouseX, mouseY);
            }
            int row6 = featuresY + UTILS_FIREBALL_Y;
            if (isRowVisible(row6, chipH, viewportTop, viewportBottom)) {
                drawChipToggle(contentX + 4, row6, chipW, chipH, "Fireball Hitbox", fireballHitboxes, true, mouseX, mouseY);
                drawChipToggle(contentX + 8 + chipW, row6, chipW, chipH, "George", george, true, mouseX, mouseY);
            }
            int colorY = featuresY + UTILS_FIREBALL_COLOR_Y;
            if (fireballHitboxes && isRowVisible(colorY, 22, viewportTop, viewportBottom)) {
                drawTextLeft("Color", contentX + 8, colorY + 6, cfgTextMuted);
                int sw = 12;
                int gap = 2;
                int startX = contentX + 52;
                for (int i = 0; i < MC_COLOR_CODES.length; i++) {
                    int sx = startX + i * (sw + gap);
                    int col = MC_COLOR_RGB[i];
                    Gui.drawRect(sx, colorY + 4, sx + sw, colorY + 16, col);
                    if (fireballHitboxColor != null && fireballHitboxColor.length() > 0
                            && Character.toLowerCase(fireballHitboxColor.charAt(0)) == MC_COLOR_CODES[i]) {
                        Gui.drawRect(sx, colorY + 3, sx + sw, colorY + 4, 0xFFFFFFFF);
                        Gui.drawRect(sx, colorY + 16, sx + sw, colorY + 17, 0xFFFFFFFF);
                        Gui.drawRect(sx, colorY + 3, sx + 1, colorY + 17, 0xFFFFFFFF);
                        Gui.drawRect(sx + sw - 1, colorY + 3, sx + sw, colorY + 17, 0xFFFFFFFF);
                    }
                }
            }
            int nickY = featuresY + UTILS_NICK_QOL_Y;
            if (isRowVisible(nickY, chipH, viewportTop, viewportBottom)) {
                drawChipToggle(contentX + 4, nickY, chipW, chipH, "Nick QOL", nickQol, true, mouseX, mouseY);
                String nickKey = listeningNickQolKey ? "..." : nickQolKeyLabel();
                drawValueChip(contentX + 8 + chipW, nickY, chipW, chipH, "Key: " + nickKey, mouseX, mouseY);
            }
        }

        drawUtilsHotkeysPanel(contentX, cy, contentW, viewportTop, viewportBottom, mouseX, mouseY, cfgTextAccent, cfgTextMuted);
        drawResourceTimerPanel(contentX, cy, contentW, viewportTop, viewportBottom, mouseX, mouseY, cfgTextAccent, cfgTextMuted);

        int glTitleY = cy + UTILS_GL_TITLE_Y;
        drawPanelSectionTitle(contentX, glTitleY, "GL Message", cfgTextAccent, viewportTop, viewportBottom);
        int glFieldY = cy + UTILS_GL_FIELD_Y;
        if (isRowVisible(glFieldY, 34, viewportTop, viewportBottom)) {
            drawInputField(contentX, glFieldY, contentW - 28, "", gl_message, true, activeInput == 4, mouseX, mouseY);
            drawClearButton(contentX + contentW - 24, glFieldY, mouseX, mouseY);
        }
    }

    private void drawUtilsHotkeysPanel(int contentX, int cy, int contentW, int viewportTop, int viewportBottom,
                                       int mouseX, int mouseY, int cfgTextAccent, int cfgTextMuted) {
        int titleY = cy + UTILS_HOTKEYS_TITLE_Y;
        drawPanelSectionTitle(contentX, titleY, "Hotkeys", cfgTextAccent, viewportTop, viewportBottom);
        int cardY = cy + UTILS_HOTKEYS_SECTION_Y;
        if (!isRowVisible(cardY, UTILS_HOTKEYS_CARD_H, viewportTop, viewportBottom)
                && !isRowVisible(cardY + UTILS_HOTKEYS_CARD_H - 1, 1, viewportTop, viewportBottom)) {
            return;
        }
        drawSectionCardClipped(contentX, cardY, contentW, UTILS_HOTKEYS_CARD_H, viewportTop, viewportBottom);
        int chipH = 22;
        int keyW = 72;
        for (int i = 0; i < UTILS_HK_LABELS.length; i++) {
            int rowY = cardY + 8 + (i * 26);
            if (!isRowVisible(rowY, chipH, viewportTop, viewportBottom)) continue;
            drawTextLeft(UTILS_HK_LABELS[i], contentX + 8, rowY + 6, cfgTextMuted);
            drawValueChip(contentX + contentW - 4 - keyW, rowY, keyW, chipH, utilsHotkeyLabel(i), mouseX, mouseY);
        }
    }

    private boolean handleUtilsHotkeysPanelClicks(int contentX, int cy, int contentW, int panelTop, int panelBottom, int mouseX, int mouseY) {
        int cardY = cy + UTILS_HOTKEYS_SECTION_Y;
        int chipH = 22;
        int keyW = 72;
        for (int i = 0; i < UTILS_HK_LABELS.length; i++) {
            int rowY = cardY + 8 + (i * 26);
            if (hitChip(contentX + contentW - 4 - keyW, rowY, keyW, chipH, panelTop, panelBottom, mouseX, mouseY)) {
                listeningUtilsHk = i;
                listeningHoldRdKey = false;
                listeningNickQolKey = false;
                listeningQuickbuyIndex = -1;
                return true;
            }
        }
        if (listeningUtilsHk >= 0) {
            listeningUtilsHk = -1;
            return true;
        }
        return false;
    }

    private void openHudEditMode() {
        hudEditMode = true;
        listeningUtilsHk = -1;
        listeningHoldRdKey = false;
        listeningNickQolKey = false;
        listeningQuickbuyIndex = -1;
        closeScreenSafe();
    }

    private boolean handleUtilsPanelClicks(int contentX, int cy, int contentW, int panelTop, int panelBottom, int mouseX, int mouseY) {
        int featuresY = cy + UTILS_FEATURES_Y;
        int chipH = 22;
        int chipW = (contentW - 12) / 2;
        int row1 = featuresY + 8;
        int row2 = featuresY + 36;
        int row3 = featuresY + 64;
        int row4 = featuresY + UTILS_SHOT_DISTANCE_Y;
        int row5 = featuresY + UTILS_BED_DC_Y;
        if (hitChip(contentX + 4, row1, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { autoWho = !autoWho; listeningHoldRdKey = false; listeningQuickbuyIndex = -1; return true; }
        if (hitChip(contentX + 8 + chipW, row1, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { partyDetector = !partyDetector; listeningHoldRdKey = false; listeningQuickbuyIndex = -1; return true; }
        if (hitChip(contentX + 4, row2, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { numberDenicker = !numberDenicker; listeningHoldRdKey = false; listeningQuickbuyIndex = -1; return true; }
        if (hitChip(contentX + 8 + chipW, row2, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { autoGL = !autoGL; listeningHoldRdKey = false; listeningQuickbuyIndex = -1; return true; }
        if (hitChip(contentX + 4, row3, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { fastBuy = !fastBuy; listeningHoldRdKey = false; listeningQuickbuyIndex = -1; return true; }
        if (hitChip(contentX + 8 + chipW, row3, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { coloredHitboxes = !coloredHitboxes; listeningHoldRdKey = false; listeningQuickbuyIndex = -1; return true; }
        if (coloredHitboxes) {
            int sliderY = featuresY + UTILS_HITBOX_SLIDER_Y;
            int sliderH = UTILS_HITBOX_SLIDER_H;
            int trackX = hitboxSliderTrackX(contentX);
            int trackW = hitboxSliderTrackW(contentW);
            if (hitChip(trackX, sliderY, trackW, sliderH, panelTop, panelBottom, mouseX, mouseY)) {
                hitboxThickness = hitboxThicknessFromMouse(trackX, trackW, mouseX);
                draggingHitboxSlider = true;
                hitboxSliderTrackX = trackX;
                hitboxSliderTrackW = trackW;
                listeningHoldRdKey = false;
                listeningQuickbuyIndex = -1;
                return true;
            }
        }
        if (hitChip(contentX + 4, row4, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { dragonHitboxes = !dragonHitboxes; listeningHoldRdKey = false; listeningQuickbuyIndex = -1; return true; }
        if (hitChip(contentX + 8 + chipW, row4, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { arrowDistance = !arrowDistance; listeningHoldRdKey = false; listeningQuickbuyIndex = -1; return true; }
        if (hitChip(contentX + 4, row5, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { bedDisconnectTimer = !bedDisconnectTimer; listeningHoldRdKey = false; listeningQuickbuyIndex = -1; return true; }
        if (hitChip(contentX + 8 + chipW, row5, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { tradeIndicator = !tradeIndicator; listeningHoldRdKey = false; listeningQuickbuyIndex = -1; return true; }
        int row6 = featuresY + UTILS_FIREBALL_Y;
        if (hitChip(contentX + 4, row6, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) {
            fireballHitboxes = !fireballHitboxes;
            listeningHoldRdKey = false;
            listeningNickQolKey = false;
            listeningQuickbuyIndex = -1;
            return true;
        }
        if (hitChip(contentX + 8 + chipW, row6, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) {
            george = !george;
            listeningHoldRdKey = false;
            listeningNickQolKey = false;
            listeningQuickbuyIndex = -1;
            return true;
        }
        if (fireballHitboxes) {
            int colorY = featuresY + UTILS_FIREBALL_COLOR_Y;
            int sw = 12;
            int gap = 2;
            int startX = contentX + 52;
            for (int i = 0; i < MC_COLOR_CODES.length; i++) {
                int sx = startX + i * (sw + gap);
                if (hitChip(sx, colorY + 3, sw, 14, panelTop, panelBottom, mouseX, mouseY)) {
                    fireballHitboxColor = String.valueOf(MC_COLOR_CODES[i]);
                    listeningHoldRdKey = false;
                    listeningNickQolKey = false;
                    listeningQuickbuyIndex = -1;
                    listeningUtilsHk = -1;
                    return true;
                }
            }
        }
        int nickY = featuresY + UTILS_NICK_QOL_Y;
        if (hitChip(contentX + 4, nickY, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) {
            nickQol = !nickQol;
            listeningHoldRdKey = false;
            listeningNickQolKey = false;
            listeningQuickbuyIndex = -1;
            listeningUtilsHk = -1;
            return true;
        }
        if (hitChip(contentX + 8 + chipW, nickY, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) {
            listeningNickQolKey = true;
            listeningHoldRdKey = false;
            listeningQuickbuyIndex = -1;
            listeningUtilsHk = -1;
            return true;
        }

        if (handleUtilsHotkeysPanelClicks(contentX, cy, contentW, panelTop, panelBottom, mouseX, mouseY)) return true;
        return handleResourceTimerPanelClicks(contentX, cy, contentW, panelTop, panelBottom, mouseX, mouseY);
    }

    private void drawBlackchifyPanels(int contentX, int cy, int contentW, int viewportTop, int viewportBottom,
                                      int mouseX, int mouseY, int cfgTextAccent, int cfgTextMuted) {
        int chipH = 22;
        int chipW = (contentW - 12) / 2;
        int holdY = cy + 4;
        int holdH = 40;
        if (isRowVisible(holdY, holdH, viewportTop, viewportBottom)) {
            drawSectionCard(contentX, holdY, contentW, holdH);
            if (isRowVisible(holdY + 8, chipH, viewportTop, viewportBottom)) {
                drawChipToggle(contentX + 4, holdY + 8, chipW, chipH, "Hold RD", holdRdEnabled, true, mouseX, mouseY);
                String keyName = listeningHoldRdKey ? "..." : holdRdKeyLabel();
                drawValueChip(contentX + 8 + chipW, holdY + 8, chipW, chipH, "Key: " + keyName, mouseX, mouseY);
            }
        }

        int qbY = holdY + holdH + 12;
        int qbHeaderH = 50;
        int qbCardH = qbHeaderH + (QUICKBUY_IDS.length * 26) + 8;
        drawSectionCardClipped(contentX, qbY, contentW, qbCardH, viewportTop, viewportBottom);

        if (isRowVisible(qbY + 8, chipH, viewportTop, viewportBottom)) {
            drawChipToggle(contentX + 4, qbY + 8, chipW, chipH, "Quickbuy", quickbuyEnabled, true, mouseX, mouseY);
            drawValueChip(contentX + 8 + chipW, qbY + 8, chipW, chipH, "Delay: " + quickbuyDelayMs + "ms", mouseX, mouseY);
        }

        int turboW = 54;
        int slotW = 68;
        int keyW = 68;
        int gap = 4;
        for (int i = 0; i < QUICKBUY_IDS.length; i++) {
            int rowY = qbY + qbHeaderH + (i * 26);
            if (!isRowVisible(rowY, chipH, viewportTop, viewportBottom)) continue;
            boolean upg = isQuickbuyUpgrade(i);
            drawTextLeft(QUICKBUY_LABELS[i], contentX + 8, rowY + 7, cfgTextMuted);
            if (upg) {
                int keyX = contentX + contentW - 4 - keyW;
                String keyName = (listeningQuickbuyIndex == i) ? "..." : quickbuyKeyLabel(i);
                drawValueChip(keyX, rowY, keyW, chipH, keyName, mouseX, mouseY);
            } else {
                int turboX = contentX + contentW - 4 - turboW;
                int slotX = turboX - gap - slotW;
                int keyX = slotX - gap - keyW;
                String keyName = (listeningQuickbuyIndex == i) ? "..." : quickbuyKeyLabel(i);
                drawValueChip(keyX, rowY, keyW, chipH, keyName, mouseX, mouseY);
                String slotLabel = quickbuySlots[i] <= 0 ? "Slot: Off" : ("Slot: " + quickbuySlots[i]);
                drawValueChip(slotX, rowY, slotW, chipH, slotLabel, mouseX, mouseY);
                drawChipToggle(turboX, rowY, turboW, chipH, "Turbo", quickbuyTurbo[i], true, mouseX, mouseY, false);
            }
        }
    }

    private boolean handleBlackchifyPanelClicks(int contentX, int cy, int contentW, int panelTop, int panelBottom, int mouseX, int mouseY) {
        int chipH = 22;
        int chipW = (contentW - 12) / 2;
        int holdY = cy + 4;
        int qbY = holdY + 40 + 12;
        int qbHeaderH = 50;

        if (hitChip(contentX + 4, holdY + 8, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) {
            holdRdEnabled = !holdRdEnabled;
            listeningHoldRdKey = false;
            listeningNickQolKey = false;
            listeningQuickbuyIndex = -1;
            return true;
        }
        if (hitChip(contentX + 8 + chipW, holdY + 8, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) {
            listeningHoldRdKey = true;
            listeningNickQolKey = false;
            listeningQuickbuyIndex = -1;
            return true;
        }
        if (hitChip(contentX + 4, qbY + 8, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) {
            quickbuyEnabled = !quickbuyEnabled;
            listeningHoldRdKey = false;
            listeningQuickbuyIndex = -1;
            return true;
        }
        if (hitChip(contentX + 8 + chipW, qbY + 8, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) {
            cycleQuickbuyDelay();
            listeningHoldRdKey = false;
            listeningQuickbuyIndex = -1;
            return true;
        }

        int turboW = 54;
        int slotW = 68;
        int keyW = 68;
        int gap = 4;
        for (int i = 0; i < QUICKBUY_IDS.length; i++) {
            int rowY = qbY + qbHeaderH + (i * 26);
            boolean upg = isQuickbuyUpgrade(i);
            if (upg) {
                int keyX = contentX + contentW - 4 - keyW;
                if (hitChip(keyX, rowY, keyW, chipH, panelTop, panelBottom, mouseX, mouseY)) {
                    listeningQuickbuyIndex = i;
                    listeningHoldRdKey = false;
                    return true;
                }
            } else {
                int turboX = contentX + contentW - 4 - turboW;
                int slotX = turboX - gap - slotW;
                int keyX = slotX - gap - keyW;
                if (hitChip(keyX, rowY, keyW, chipH, panelTop, panelBottom, mouseX, mouseY)) {
                    listeningQuickbuyIndex = i;
                    listeningHoldRdKey = false;
                    return true;
                }
                if (hitChip(slotX, rowY, slotW, chipH, panelTop, panelBottom, mouseX, mouseY)) {
                    quickbuySlots[i] = (quickbuySlots[i] + 1) % 10;
                    syncQuickbuyBinds();
                    listeningHoldRdKey = false;
                    listeningQuickbuyIndex = -1;
                    return true;
                }
                if (hitChip(turboX, rowY, turboW, chipH, panelTop, panelBottom, mouseX, mouseY)) {
                    quickbuyTurbo[i] = !quickbuyTurbo[i];
                    syncQuickbuyBinds();
                    listeningHoldRdKey = false;
                    listeningQuickbuyIndex = -1;
                    return true;
                }
            }
        }

        if (listeningHoldRdKey || listeningQuickbuyIndex >= 0) {
            listeningHoldRdKey = false;
            listeningQuickbuyIndex = -1;
            return true;
        }
        return false;
    }

    private void drawExperimentalPanels(int contentX, int cy, int contentW, int viewportTop, int viewportBottom,
                                        int mouseX, int mouseY, int cfgTextAccent, int cfgTextMuted) {
        int cardY = cy + 4;
        int cardH = 56;
        if (isRowVisible(cardY, cardH, viewportTop, viewportBottom)) {
            drawSectionCard(contentX, cardY, contentW, cardH);
            drawChipToggle(contentX + 4, cardY + 8, contentW - 8, 22, "Party Detector IDs", partyQueueDetector, true, mouseX, mouseY);
            drawTextLeft("Queue party size via spawn entity IDs", contentX + 8, cardY + 36, cfgTextMuted);
        }
    }

    private boolean handleExperimentalPanelClicks(int contentX, int cy, int contentW, int panelTop, int panelBottom, int mouseX, int mouseY) {
        int cardY = cy + 4;
        if (hitChip(contentX + 4, cardY + 8, contentW - 8, 22, panelTop, panelBottom, mouseX, mouseY)) {
            partyQueueDetector = !partyQueueDetector;
            return true;
        }
        return false;
    }

    private void drawAnticheatPanels(int contentX, int cy, int contentW, int viewportTop, int viewportBottom,
                                     int mouseX, int mouseY, int cfgTextAccent, int cfgTextMuted) {
        int mainY = cy + AC_MAIN_Y;
        if (isRowVisible(mainY, AC_MAIN_H, viewportTop, viewportBottom)) {
            drawSectionCard(contentX, mainY, contentW, AC_MAIN_H);
            drawTextLeft("Enabled", contentX + 8, mainY + 7, anticheat ? CLR_ON : CLR_OFF);
            drawMiniSwitch(contentX + contentW - 34, mainY + 6, 28, 12, anticheat, true, mouseX, mouseY);
            drawChipToggle(contentX + 4, mainY + 24, contentW - 8, 22, "Flag Sound", ac_flagsound, anticheat, mouseX, mouseY);
        }

        int checksY = cy + AC_CHECKS_Y;
        drawPanelSectionTitle(contentX, checksY - 12, "Checks", cfgTextMuted, viewportTop, viewportBottom);
        if (isRowVisible(checksY, AC_CHECKS_H, viewportTop, viewportBottom)) {
            drawSectionCard(contentX, checksY, contentW, AC_CHECKS_H);
            int chipH = 22;
            int chipW = (contentW - 12) / 2;
            drawChipToggle(contentX + 4, checksY + 8, chipW, chipH, "Autoblock", ac_autoblock, anticheat, mouseX, mouseY);
            drawChipToggle(contentX + 8 + chipW, checksY + 8, chipW, chipH, "LegitScaff", ac_legitscaff, anticheat, mouseX, mouseY);
            drawChipToggle(contentX + 4, checksY + 36, chipW, chipH, "Keep-y", ac_scaffold, anticheat, mouseX, mouseY);
            drawChipToggle(contentX + 8 + chipW, checksY + 36, chipW, chipH, "FlatScaff", ac_scaffoldb, anticheat, mouseX, mouseY);
            drawChipToggle(contentX + 4, checksY + 64, chipW, chipH, "NoSlow", ac_noslow, anticheat, mouseX, mouseY);
        }
    }

    private boolean handleAnticheatPanelClicks(int contentX, int cy, int contentW, int panelTop, int panelBottom, int mouseX, int mouseY) {
        int mainY = cy + AC_MAIN_Y;
        if (hitCardSwitch(contentX, contentW, mainY, panelTop, panelBottom, mouseX, mouseY)) {
            anticheat = !anticheat;
            return true;
        }
        if (anticheat && hitChip(contentX + 4, mainY + 24, contentW - 8, 22, panelTop, panelBottom, mouseX, mouseY)) {
            ac_flagsound = !ac_flagsound;
            return true;
        }
        if (!anticheat) return false;

        int checksY = cy + AC_CHECKS_Y;
        int chipH = 22;
        int chipW = (contentW - 12) / 2;
        if (hitChip(contentX + 4, checksY + 8, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { ac_autoblock = !ac_autoblock; return true; }
        if (hitChip(contentX + 8 + chipW, checksY + 8, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { ac_legitscaff = !ac_legitscaff; return true; }
        if (hitChip(contentX + 4, checksY + 36, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { ac_scaffold = !ac_scaffold; return true; }
        if (hitChip(contentX + 8 + chipW, checksY + 36, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { ac_scaffoldb = !ac_scaffoldb; return true; }
        if (hitChip(contentX + 4, checksY + 64, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { ac_noslow = !ac_noslow; return true; }
        return false;
    }

    private void drawSettingsApiPanel(int contentX, int cy, int contentW, int viewportTop, int viewportBottom,
                                      int mouseX, int mouseY, int cfgTextAccent, int cfgTextMuted) {
        int cardY = cy + 16;
        int cardH = 190;
        drawPanelSectionTitle(contentX, cardY - 12, "API Keys", cfgTextMuted, viewportTop, viewportBottom);
        if (!isRowVisible(cardY, cardH, viewportTop, viewportBottom)) return;

        drawSectionCard(contentX, cardY, contentW, cardH);
        int fieldW = contentW - 52;
        int eyeX = contentX + fieldW + 4;
        int clearX = eyeX + 24;
        int baseY = cardY + 8;
        if (isRowVisible(baseY + 10, 34, viewportTop, viewportBottom)) {
            drawInputField(contentX + 4, baseY + 20, fieldW, "Hypixel API Key", api_hypixel, showHypixel, activeInput == 1, mouseX, mouseY);
            drawEyeButton(eyeX, baseY + 20, showHypixel, mouseX, mouseY);
            drawClearButton(clearX, baseY + 20, mouseX, mouseY);
        }
        if (isRowVisible(baseY + 48, 34, viewportTop, viewportBottom)) {
            drawInputField(contentX + 4, baseY + 58, fieldW, "Seraph API Key", api_seraph, showSeraph, activeInput == 2, mouseX, mouseY);
            drawEyeButton(eyeX, baseY + 58, showSeraph, mouseX, mouseY);
            drawClearButton(clearX, baseY + 58, mouseX, mouseY);
        }
        if (isRowVisible(baseY + 86, 34, viewportTop, viewportBottom)) {
            drawInputField(contentX + 4, baseY + 96, fieldW, "Vega API Key", api_aurora, showAurora, activeInput == 3, mouseX, mouseY);
            drawEyeButton(eyeX, baseY + 96, showAurora, mouseX, mouseY);
            drawClearButton(clearX, baseY + 96, mouseX, mouseY);
        }
        if (isRowVisible(baseY + 124, 52, viewportTop, viewportBottom)) {
            drawChipToggle(contentX + 4, baseY + 134, contentW - 8, 22, "Vega Proxy", vegaProxy, true, mouseX, mouseY);
            drawTextLeft("keyless stats", contentX + 8, baseY + 160, cfgTextMuted);
            drawTextLeft("vega api might not contain every stats", contentX + 8, baseY + 172, cfgTextMuted);
        }
    }

    private void drawChatOverlayPanels(int contentX, int cy, int contentW, int viewportTop, int viewportBottom,
                                       int mouseX, int mouseY, int cfgTextAccent, int cfgTextMuted) {
        int mainY = cy + CHAT_MAIN_Y;
        if (isRowVisible(mainY, CHAT_MAIN_H, viewportTop, viewportBottom)) {
            drawSectionCard(contentX, mainY, contentW, CHAT_MAIN_H);
            int chipH = 22;
            int chipW = (contentW - 16) / 3;
            drawChipToggle(contentX + 4, mainY + 8, chipW, chipH, "Compact Bl", compactBlacklist, true, mouseX, mouseY);
            drawChipToggle(contentX + 8 + chipW, mainY + 8, chipW, chipH, "In Chat", chatOverlay, true, mouseX, mouseY);
            drawChipToggle(contentX + 12 + (chipW * 2), mainY + 8, chipW, chipH, "Teammate", !ignoreTeammates, true, mouseX, mouseY);
        }

        int threshY = cy + CHAT_THRESH_Y;
        drawPanelSectionTitle(contentX, threshY - 12, "Thresholds", cfgTextMuted, viewportTop, viewportBottom);
        if (!isRowVisible(threshY, CHAT_THRESH_H, viewportTop, viewportBottom)) return;

        drawSectionCard(contentX, threshY, contentW, CHAT_THRESH_H);
        int fieldW = contentW - 36;
        int baseY = threshY + 8;
        if (isRowVisible(baseY + 10, 34, viewportTop, viewportBottom)) {
            drawInputField(contentX + 4, baseY + 20, fieldW, "Bedwars Min Stars", chatOverlayMinStars, true, activeInput == 5, mouseX, mouseY);
            drawClearButton(contentX + fieldW + 8, baseY + 20, mouseX, mouseY);
        }
        if (isRowVisible(baseY + 48, 34, viewportTop, viewportBottom)) {
            drawInputField(contentX + 4, baseY + 58, fieldW, "Bedwars Min FKDR", chatOverlayMinFkdr, true, activeInput == 6, mouseX, mouseY);
            drawClearButton(contentX + fieldW + 8, baseY + 58, mouseX, mouseY);
        }
        if (isRowVisible(baseY + 86, 34, viewportTop, viewportBottom)) {
            drawInputField(contentX + 4, baseY + 96, fieldW, "SkyWars Min KDR", chatOverlayMinSwKdr, true, activeInput == 7, mouseX, mouseY);
            drawClearButton(contentX + fieldW + 8, baseY + 96, mouseX, mouseY);
        }
    }

    private boolean handleChatOverlayPanelClicks(int contentX, int cy, int contentW, int panelTop, int panelBottom, int mouseX, int mouseY) {
        int mainY = cy + CHAT_MAIN_Y;
        int chipH = 22;
        int chipW = (contentW - 16) / 3;
        if (hitChip(contentX + 4, mainY + 8, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { compactBlacklist = !compactBlacklist; return true; }
        if (hitChip(contentX + 8 + chipW, mainY + 8, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { chatOverlay = !chatOverlay; return true; }
        if (hitChip(contentX + 12 + (chipW * 2), mainY + 8, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { ignoreTeammates = !ignoreTeammates; return true; }
        return false;
    }

    private static int clampSessionOpacity(int v) {
        if (v < 0) return 0;
        if (v > 255) return 255;
        return v;
    }

    private static int clampSessionScale(int v) {
        if (v < 50) return 50;
        if (v > 150) return 150;
        return v;
    }

    private static int clampSessionPos(int v) {
        if (v < 0) return 0;
        if (v > 1000) return 1000;
        return v;
    }

    private void drawSessionStatsMainPanel(int contentX, int cy, int contentW, int viewportTop, int viewportBottom,
                                         int mouseX, int mouseY) {
        int cardY = cy + SESSION_STATS_Y;
        if (!isRowVisible(cardY, SESSION_STATS_H, viewportTop, viewportBottom)) return;

        drawSectionCardClipped(contentX, cardY, contentW, SESSION_STATS_H, viewportTop, viewportBottom);

        if (isRowVisible(cardY + 6, 18, viewportTop, viewportBottom)) {
            drawTextLeft("Enabled", contentX + 8, cardY + 7, sessionStats ? CLR_ON : CLR_OFF);
            drawMiniSwitch(contentX + contentW - 34, cardY + 6, 28, 12, sessionStats, true, mouseX, mouseY);
        }

        int chipH = 22;
        int chipW = (contentW - 12) / 2;
        int row1 = cardY + 28;
        int row2 = cardY + 54;
        int row3 = cardY + 80;
        int row4 = cardY + 106;
        if (isRowVisible(row1, chipH, viewportTop, viewportBottom)) {
            drawChipToggle(contentX + 4, row1, chipW, chipH, "Game Stats", sessionStatsGame, sessionStats, mouseX, mouseY);
            drawChipToggle(contentX + 8 + chipW, row1, chipW, chipH, "Final Kills", sessionStatsFinalKills, sessionStats, mouseX, mouseY);
        }
        if (isRowVisible(row2, chipH, viewportTop, viewportBottom)) {
            drawChipToggle(contentX + 4, row2, chipW, chipH, "Beds", sessionStatsBeds, sessionStats, mouseX, mouseY);
            drawChipToggle(contentX + 8 + chipW, row2, chipW, chipH, "Kills", sessionStatsKills, sessionStats, mouseX, mouseY);
        }
        if (isRowVisible(row3, chipH, viewportTop, viewportBottom)) {
            drawChipToggle(contentX + 4, row3, chipW, chipH, "Wins", sessionStatsWins, sessionStats, mouseX, mouseY);
            drawChipToggle(contentX + 8 + chipW, row3, chipW, chipH, "Winstreak", sessionStatsWinstreak, sessionStats, mouseX, mouseY);
        }
        if (isRowVisible(row4, chipH, viewportTop, viewportBottom)) {
            drawChipToggle(contentX + 4, row4, chipW, chipH, "Slumber Tickets", sessionStatsSlumberTickets, sessionStats, mouseX, mouseY);
            drawChipToggle(contentX + 8 + chipW, row4, chipW, chipH, "Session Games", sessionStatsSessionGames, sessionStats, mouseX, mouseY);
        }
    }

    private void drawSessionDisplayPanel(int contentX, int cy, int contentW, int viewportTop, int viewportBottom,
                                         int mouseX, int mouseY, int cfgTextMuted) {
        int titleY = cy + SESSION_DISPLAY_TITLE_Y;
        drawPanelSectionTitle(contentX, titleY, "Display", cfgTextMuted, viewportTop, viewportBottom);

        int cardY = cy + SESSION_DISPLAY_SECTION_Y;
        if (!isRowVisible(cardY, SESSION_DISPLAY_CARD_H, viewportTop, viewportBottom)) return;

        drawSectionCardClipped(contentX, cardY, contentW, SESSION_DISPLAY_CARD_H, viewportTop, viewportBottom);

        int chipH = 22;
        int chipW = (contentW - 12) / 2;
        int row1 = cardY + 8;
        int row2 = cardY + 36;
        int row3 = cardY + 64;
        if (isRowVisible(row1, chipH, viewportTop, viewportBottom)) {
            drawChipToggle(contentX + 4, row1, chipW, chipH, "Game Time", sessionStatsGameTime, sessionStats, mouseX, mouseY);
            drawChipToggle(contentX + 8 + chipW, row1, chipW, chipH, "Avg Time", sessionStatsAvgTime, sessionStats, mouseX, mouseY);
        }
        if (isRowVisible(row2, chipH, viewportTop, viewportBottom)) {
            drawChipToggle(contentX + 4, row2, chipW, chipH, "Session Time", sessionStatsSessionTime, sessionStats, mouseX, mouseY);
            drawChipToggle(contentX + 8 + chipW, row2, chipW, chipH, "Text Shadow", sessionStatsTextShadow, sessionStats, mouseX, mouseY);
        }
        if (isRowVisible(row3, chipH, viewportTop, viewportBottom)) {
            drawActionChip(contentX + 4, row3, contentW - 8, chipH, "Reset Session", mouseX, mouseY);
        }

        int opacityY = cardY + 92;
        if (isRowVisible(opacityY, 22, viewportTop, viewportBottom)) {
            int valueW = 48;
            int valueX = contentX + 86;
            int minusX = valueX + valueW + 6;
            int plusX = minusX + 26;
            drawTextLeft("BG Opacity", contentX + 8, opacityY + 5, cfgTextMuted);
            drawValueChip(valueX, opacityY, valueW, 22, clampSessionOpacity(sessionStatsBgOpacity) + "", mouseX, mouseY);
            drawActionChip(minusX, opacityY, 24, 22, "-", mouseX, mouseY);
            drawActionChip(plusX, opacityY, 24, 22, "+", mouseX, mouseY);
        }

        int scaleY = cardY + 118;
        if (isRowVisible(scaleY, 22, viewportTop, viewportBottom)) {
            int valueW = 48;
            int valueX = contentX + 70;
            int minusX = valueX + valueW + 6;
            int plusX = minusX + 26;
            drawTextLeft("Scale", contentX + 8, scaleY + 5, cfgTextMuted);
            drawValueChip(valueX, scaleY, valueW, 22, clampSessionScale(sessionStatsScale) + "%", mouseX, mouseY);
            drawActionChip(minusX, scaleY, 24, 22, "-", mouseX, mouseY);
            drawActionChip(plusX, scaleY, 24, 22, "+", mouseX, mouseY);
        }

        int editY = cardY + 146;
        if (isRowVisible(editY, 22, viewportTop, viewportBottom)) {
            drawActionChip(contentX + 4, editY, contentW - 8, 22, "Edit HUD Positions", mouseX, mouseY);
        }
    }

    private void drawSessionStatsPanels(int contentX, int cy, int contentW, int viewportTop, int viewportBottom,
                                        int mouseX, int mouseY, int cfgTextAccent, int cfgTextMuted) {
        drawSessionStatsMainPanel(contentX, cy, contentW, viewportTop, viewportBottom, mouseX, mouseY);
        drawSessionDisplayPanel(contentX, cy, contentW, viewportTop, viewportBottom, mouseX, mouseY, cfgTextMuted);
    }

    private boolean handleSessionStatsMainPanelClicks(int contentX, int cy, int contentW, int panelTop, int panelBottom, int mouseX, int mouseY) {
        int cardY = cy + SESSION_STATS_Y;
        if (hitCardSwitch(contentX, contentW, cardY, panelTop, panelBottom, mouseX, mouseY)) {
            sessionStats = !sessionStats;
            return true;
        }

        int chipH = 22;
        int chipW = (contentW - 12) / 2;
        int row1 = cardY + 28;
        int row2 = cardY + 54;
        int row3 = cardY + 80;
        int row4 = cardY + 106;
        if (hitChip(contentX + 4, row1, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { sessionStatsGame = !sessionStatsGame; return true; }
        if (hitChip(contentX + 8 + chipW, row1, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { sessionStatsFinalKills = !sessionStatsFinalKills; return true; }
        if (hitChip(contentX + 4, row2, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { sessionStatsBeds = !sessionStatsBeds; return true; }
        if (hitChip(contentX + 8 + chipW, row2, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { sessionStatsKills = !sessionStatsKills; return true; }
        if (hitChip(contentX + 4, row3, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { sessionStatsWins = !sessionStatsWins; return true; }
        if (hitChip(contentX + 8 + chipW, row3, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { sessionStatsWinstreak = !sessionStatsWinstreak; return true; }
        if (hitChip(contentX + 4, row4, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { sessionStatsSlumberTickets = !sessionStatsSlumberTickets; return true; }
        if (hitChip(contentX + 8 + chipW, row4, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { sessionStatsSessionGames = !sessionStatsSessionGames; return true; }
        return false;
    }

    private boolean handleSessionDisplayPanelClicks(int contentX, int cy, int contentW, int panelTop, int panelBottom, int mouseX, int mouseY) {
        int cardY = cy + SESSION_DISPLAY_SECTION_Y;
        int chipH = 22;
        int chipW = (contentW - 12) / 2;
        int row1 = cardY + 8;
        int row2 = cardY + 36;
        int row3 = cardY + 64;
        if (hitChip(contentX + 4, row1, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { sessionStatsGameTime = !sessionStatsGameTime; return true; }
        if (hitChip(contentX + 8 + chipW, row1, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { sessionStatsAvgTime = !sessionStatsAvgTime; return true; }
        if (hitChip(contentX + 4, row2, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { sessionStatsSessionTime = !sessionStatsSessionTime; return true; }
        if (hitChip(contentX + 8 + chipW, row2, chipW, chipH, panelTop, panelBottom, mouseX, mouseY)) { sessionStatsTextShadow = !sessionStatsTextShadow; return true; }
        if (hitChip(contentX + 4, row3, contentW - 8, chipH, panelTop, panelBottom, mouseX, mouseY)) {
            sessionStatsResetPending = true;
            return true;
        }

        int opacityY = cardY + 92;
        int valueW = 48;
        int valueX = contentX + 86;
        int minusX = valueX + valueW + 6;
        int plusX = minusX + 26;
        if (hitChip(minusX, opacityY, 24, 22, panelTop, panelBottom, mouseX, mouseY)) {
            sessionStatsBgOpacity = clampSessionOpacity(sessionStatsBgOpacity - 10);
            return true;
        }
        if (hitChip(plusX, opacityY, 24, 22, panelTop, panelBottom, mouseX, mouseY)) {
            sessionStatsBgOpacity = clampSessionOpacity(sessionStatsBgOpacity + 10);
            return true;
        }

        int scaleY = cardY + 118;
        valueX = contentX + 70;
        minusX = valueX + valueW + 6;
        plusX = minusX + 26;
        if (hitChip(minusX, scaleY, 24, 22, panelTop, panelBottom, mouseX, mouseY)) {
            sessionStatsScale = clampSessionScale(sessionStatsScale - 5);
            return true;
        }
        if (hitChip(plusX, scaleY, 24, 22, panelTop, panelBottom, mouseX, mouseY)) {
            sessionStatsScale = clampSessionScale(sessionStatsScale + 5);
            return true;
        }

        int editY = cardY + 146;
        if (hitChip(contentX + 4, editY, contentW - 8, 22, panelTop, panelBottom, mouseX, mouseY)) {
            openHudEditMode();
            return true;
        }
        return false;
    }

    private boolean handleSessionStatsPanelClicks(int contentX, int cy, int contentW, int panelTop, int panelBottom, int mouseX, int mouseY) {
        if (handleSessionStatsMainPanelClicks(contentX, cy, contentW, panelTop, panelBottom, mouseX, mouseY)) return true;
        if (handleSessionDisplayPanelClicks(contentX, cy, contentW, panelTop, panelBottom, mouseX, mouseY)) return true;
        return false;
    }

    private void drawOverlayHeaderCard(int contentX, int cyFixed, int contentW, int viewportTop, int viewportBottom,
                                       int mouseX, int mouseY, int cfgTextAccent, int cfgTextMuted) {
        if (!isRowVisible(cyFixed, OVERLAY_FIXED_HEADER_H, viewportTop, viewportBottom)) return;

        drawSectionCard(contentX, cyFixed, contentW, OVERLAY_FIXED_HEADER_H);
        drawTextLeft("Enabled", contentX + 8, cyFixed + 7, tabOverlay ? CLR_ON : CLR_OFF);
        drawMiniSwitch(contentX + contentW - 34, cyFixed + 6, 28, 12, tabOverlay, true, mouseX, mouseY);

        drawTextLeft("Opacity", contentX + 8, cyFixed + 28, cfgTextMuted);
        int opacityChipW = 72;
        drawValueChip(contentX + 4, cyFixed + 40, opacityChipW, 22,
            normalizeOpacity(overlayOpacity) + "%", mouseX, mouseY);
        int minusX = contentX + opacityChipW + 10;
        int plusX = minusX + 28;
        drawActionChip(minusX, cyFixed + 40, 24, 22, "-", mouseX, mouseY);
        drawActionChip(plusX, cyFixed + 40, 24, 22, "+", mouseX, mouseY);

        String activeLabel = overlayGamemodeActive == null || "unknown".equals(overlayGamemodeActive)
            ? "unknown" : overlayGamemodeActive;
        drawTextLeft("Active: " + activeLabel, contentX + 8, cyFixed + 68, cfgTextMuted);
        int selectedMode = 0;
        for (int m = 0; m < OVERLAY_GAMEMODES.length; m++) {
            if (OVERLAY_GAMEMODES[m].equals(overlayGamemodeEdit)) selectedMode = m;
        }
        drawSegmentedChoice(contentX + 4, cyFixed + 80, contentW - 8, 20, OVERLAY_GAMEMODE_LABELS, selectedMode, true, mouseX, mouseY);
    }

    private boolean handleOverlayHeaderClicks(int contentX, int cyFixed, int contentW, int panelTop, int panelBottom, int mouseX, int mouseY) {
        if (hitCardSwitch(contentX, contentW, cyFixed, panelTop, panelBottom, mouseX, mouseY)) {
            tabOverlay = !tabOverlay;
            return true;
        }
        int opacityChipW = 72;
        int minusX = contentX + opacityChipW + 10;
        int plusX = minusX + 28;
        if (hitChip(minusX, cyFixed + 40, 24, 22, panelTop, panelBottom, mouseX, mouseY)) {
            overlayOpacity = normalizeOpacity(overlayOpacity - 5);
            return true;
        }
        if (hitChip(plusX, cyFixed + 40, 24, 22, panelTop, panelBottom, mouseX, mouseY)) {
            overlayOpacity = normalizeOpacity(overlayOpacity + 5);
            return true;
        }
        int segY = cyFixed + 80;
        int segH = 20;
        int segW = (contentW - 8 - 6) / OVERLAY_GAMEMODES.length;
        for (int m = 0; m < OVERLAY_GAMEMODES.length; m++) {
            int sx = contentX + 4 + (m * (segW + 2));
            if (hitChip(sx, segY, segW, segH, panelTop, panelBottom, mouseX, mouseY)) {
                setOverlayGamemodeEdit(OVERLAY_GAMEMODES[m]);
                return true;
            }
        }
        return false;
    }

    private void drawTogglePair(int x, int y, int w, String name, boolean state, boolean enabled, int mouseX, int mouseY) {
        drawToggle(x, y, w, name, state, enabled, mouseX, mouseY);
    }

    public void initGui() {
        this.buttonList.clear();
        winW = 470;
        winH = 285;
        winX = this.width / 2 - winW / 2;
        winY = this.height / 2 - winH / 2;
        int viewportHeight = winH - HEADER_H - FOOTER_H - (PANEL_INSET * 2);
        utilsScroll = 0;
        settingsScroll = 0;
        chatOverlayScroll = 0;
        sessionStatsScroll = 0;
        utilsScroll = clampScroll(utilsScroll, gameplayPanelContentHeight(), viewportHeight);
        settingsScroll = clampScroll(settingsScroll, getPanelContentHeight(0), viewportHeight);
        chatOverlayScroll = clampScroll(chatOverlayScroll, getPanelContentHeight(4), viewportHeight);
        sessionStatsScroll = clampScroll(sessionStatsScroll, getPanelContentHeight(5), viewportHeight);
    }

    public void handleMouseInput() {
        super.handleMouseInput();
        anticheatScroll = 0;
        int wheel = Mouse.getEventDWheel();
        if (wheel != 0) {
            if (selectedTheme == 1) {
                return;
            }
            int delta = wheel > 0 ? 18 : -18;
            int viewportHeight = winH - HEADER_H - FOOTER_H - (PANEL_INSET * 2);
            int contentHeight = getPanelContentHeight(selectedTheme);
            int next = getPanelScroll(selectedTheme) + delta;
            next = clampScroll(next, contentHeight, viewportHeight);
            setPanelScroll(selectedTheme, next);
        }

        int themeContentHeight = THEMES.length * 28;
        int themeViewportHeight = winH - 70;
        int minThemeScroll = Math.min(0, themeViewportHeight - themeContentHeight);
        if (themeScroll > 0) themeScroll = 0;
        if (themeScroll < minThemeScroll) themeScroll = minThemeScroll;
    }

    protected void mouseClicked(int mouseX, int mouseY, int mouseButton) {
        super.mouseClicked(mouseX, mouseY, mouseButton);
        // Capture mouse binds while listening for a Quickbuy hotkey (incl. middle click).
        if (listeningQuickbuyIndex >= 0 && mouseButton >= 0) {
            quickbuyKeys[listeningQuickbuyIndex] = -100 - mouseButton;
            listeningQuickbuyIndex = -1;
            syncQuickbuyBinds();
            playUiClickSound();
            return;
        }
        if (mouseButton != 0) return;
        boolean clicked = false;

        int leftX = winX + PANEL_INSET;
        int leftY = winY + HEADER_H + PANEL_INSET;
        int leftW = SIDEBAR_W;
        int leftH = winH - HEADER_H - FOOTER_H - (PANEL_INSET * 2);
        int itemH = 24;

        for (int i = 0; i < THEMES.length; i++) {
            int y = leftY + (i * 28) + themeScroll;
            if (isHoveredRect(leftX, y, leftW, itemH, mouseX, mouseY)) {
                selectedTheme = i;
                if (i == 2) {
                    int viewportHeight = winH - HEADER_H - FOOTER_H - (PANEL_INSET * 2);
                    utilsScroll = clampScroll(utilsScroll, gameplayPanelContentHeight(), viewportHeight);
                }
                playUiClickSound();
                return;
            }
        }

        int contentX = leftX + leftW + CONTENT_GAP;
        int contentY = leftY + 5;
        int panelRight = winX + winW - PANEL_INSET;
        int scrollbarX = panelRight - SCROLLBAR_W - SCROLLBAR_GAP;
        int contentW = scrollbarX - CONTENT_INNER_GAP - contentX;
        int panelTop = leftY;
        int panelBottom = leftY + leftH;
        int offset = getPanelScroll(selectedTheme);
        int cy = contentY + offset;

        int fieldW = contentW - 28;
        int eyeX = contentX + fieldW + 4;

        if (selectedTheme == 1) {
            activeInput = 0;
            if (handleAnticheatPanelClicks(contentX, cy, contentW, panelTop, panelBottom, mouseX, mouseY)) clicked = true;
        } else if (selectedTheme == 0) {
            fieldW = contentW - 52;
            eyeX = contentX + fieldW + 4;
            int clearX = eyeX + 24;
            int cardY = cy + 16;
            int baseY = cardY + 8;
            if (isRowVisible(baseY + 10, 34, panelTop, panelBottom) && isHoveredRect(contentX + 4, baseY + 20, fieldW, 20, mouseX, mouseY)) { activeInput = 1; clicked = true; }
            else if (isRowVisible(baseY + 48, 34, panelTop, panelBottom) && isHoveredRect(contentX + 4, baseY + 58, fieldW, 20, mouseX, mouseY)) { activeInput = 2; clicked = true; }
            else if (isRowVisible(baseY + 86, 34, panelTop, panelBottom) && isHoveredRect(contentX + 4, baseY + 96, fieldW, 20, mouseX, mouseY)) { activeInput = 3; clicked = true; }
            else activeInput = 0;

            if (isRowVisible(baseY + 10, 34, panelTop, panelBottom) && isHoveredRect(eyeX, baseY + 20, 20, 20, mouseX, mouseY)) { showHypixel = !showHypixel; clicked = true; }
            if (isRowVisible(baseY + 48, 34, panelTop, panelBottom) && isHoveredRect(eyeX, baseY + 58, 20, 20, mouseX, mouseY)) { showSeraph = !showSeraph; clicked = true; }
            if (isRowVisible(baseY + 86, 34, panelTop, panelBottom) && isHoveredRect(eyeX, baseY + 96, 20, 20, mouseX, mouseY)) { showAurora = !showAurora; clicked = true; }
            if (hitChip(contentX + 4, baseY + 134, contentW - 8, 22, panelTop, panelBottom, mouseX, mouseY)) { vegaProxy = !vegaProxy; clicked = true; }

            if (isRowVisible(baseY + 10, 34, panelTop, panelBottom) && isHoveredRect(clearX, baseY + 20, 20, 20, mouseX, mouseY)) { api_hypixel = ""; activeInput = 0; clicked = true; }
            if (isRowVisible(baseY + 48, 34, panelTop, panelBottom) && isHoveredRect(clearX, baseY + 58, 20, 20, mouseX, mouseY)) { api_seraph = ""; activeInput = 0; clicked = true; }
            if (isRowVisible(baseY + 86, 34, panelTop, panelBottom) && isHoveredRect(clearX, baseY + 96, 20, 20, mouseX, mouseY)) { api_aurora = ""; activeInput = 0; clicked = true; }
        } else if (selectedTheme == 2) {
            int glFieldY = cy + UTILS_GL_FIELD_Y;
            if (isRowVisible(glFieldY, 34, panelTop, panelBottom) && isHoveredRect(contentX, glFieldY, fieldW, 20, mouseX, mouseY)) { activeInput = 4; clicked = true; }
            else activeInput = 0;
            if (isRowVisible(glFieldY, 34, panelTop, panelBottom) && isHoveredRect(contentX + fieldW + 4, glFieldY, 20, 20, mouseX, mouseY)) { gl_message = ""; activeInput = 0; clicked = true; }
            if (!clicked && handleUtilsPanelClicks(contentX, cy, contentW, panelTop, panelBottom, mouseX, mouseY)) clicked = true;
        } else if (selectedTheme == 3) {
            int cyFixed = contentY;
            int cyList = contentY + OVERLAY_FIXED_HEADER_H + offset;
            activeInput = 0;
            if (handleOverlayHeaderClicks(contentX, cyFixed, contentW, panelTop, panelBottom, mouseX, mouseY)) clicked = true;

            int listTop = contentY + OVERLAY_FIXED_HEADER_H;
            if (listTop < panelTop) listTop = panelTop;
            int rowY = cyList + 20;
            int rowH = 20;
            int[] sorted = getOverlaySortedIndexes();
            for (int pos = 0; pos < sorted.length; pos++) {
                int idx = sorted[pos];
                int y = rowY + (pos * 24);
                if (!isRowVisible(y, rowH, listTop, panelBottom)) continue;

                int upX = contentX + contentW - 34;
                int downX = contentX + contentW - 17;

                if (isHoveredRect(upX, y, 14, rowH, mouseX, mouseY)) {
                    moveOverlayColumn(idx, -1);
                    overlaySelectedIndex = idx;
                    clicked = true;
                    break;
                }
                if (isHoveredRect(downX, y, 14, rowH, mouseX, mouseY)) {
                    moveOverlayColumn(idx, 1);
                    overlaySelectedIndex = idx;
                    clicked = true;
                    break;
                }

                if (isHoveredRect(contentX, y, contentW - 36, rowH, mouseX, mouseY)) {
                    overlayColumnEnabled[idx] = !overlayColumnEnabled[idx];
                    overlaySelectedIndex = idx;
                    clicked = true;
                    break;
                }
            }
        } else if (selectedTheme == 4) {
            fieldW = contentW - 28;
            int threshY = cy + CHAT_THRESH_Y;
            int baseY = threshY + 8;
            if (isRowVisible(baseY + 10, 34, panelTop, panelBottom) && isHoveredRect(contentX + 4, baseY + 20, contentW - 36, 20, mouseX, mouseY)) { activeInput = 5; clicked = true; }
            else if (isRowVisible(baseY + 48, 34, panelTop, panelBottom) && isHoveredRect(contentX + 4, baseY + 58, contentW - 36, 20, mouseX, mouseY)) { activeInput = 6; clicked = true; }
            else if (isRowVisible(baseY + 86, 34, panelTop, panelBottom) && isHoveredRect(contentX + 4, baseY + 96, contentW - 36, 20, mouseX, mouseY)) { activeInput = 7; clicked = true; }
            else activeInput = 0;

            if (isRowVisible(baseY + 10, 34, panelTop, panelBottom) && isHoveredRect(contentX + contentW - 24, baseY + 20, 20, 20, mouseX, mouseY)) { chatOverlayMinStars = ""; activeInput = 0; clicked = true; }
            if (isRowVisible(baseY + 48, 34, panelTop, panelBottom) && isHoveredRect(contentX + contentW - 24, baseY + 58, 20, 20, mouseX, mouseY)) { chatOverlayMinFkdr = ""; activeInput = 0; clicked = true; }
            if (isRowVisible(baseY + 86, 34, panelTop, panelBottom) && isHoveredRect(contentX + contentW - 24, baseY + 96, 20, 20, mouseX, mouseY)) { chatOverlayMinSwKdr = ""; activeInput = 0; clicked = true; }

            if (!clicked && handleChatOverlayPanelClicks(contentX, cy, contentW, panelTop, panelBottom, mouseX, mouseY)) clicked = true;
        } else if (selectedTheme == 5) {
            activeInput = 0;
            if (!clicked && handleSessionStatsPanelClicks(contentX, cy, contentW, panelTop, panelBottom, mouseX, mouseY)) clicked = true;
        } else if (selectedTheme == 6) {
            activeInput = 0;
            if (!clicked && handleExperimentalPanelClicks(contentX, cy, contentW, panelTop, panelBottom, mouseX, mouseY)) clicked = true;
        } else if (selectedTheme == 7) {
            activeInput = 0;
            if (!clicked && handleBlackchifyPanelClicks(contentX, cy, contentW, panelTop, panelBottom, mouseX, mouseY)) clicked = true;
        }

        if (clicked) playUiClickSound();

    }

    protected void keyTyped(char typedChar, int keyCode) {
        if (listeningUtilsHk >= 0) {
            if (keyCode == 1) {
                setUtilsHotkey(listeningUtilsHk, 0);
                listeningUtilsHk = -1;
                return;
            }
            if (keyCode > 0) {
                setUtilsHotkey(listeningUtilsHk, keyCode);
                listeningUtilsHk = -1;
            }
            return;
        }
        if (listeningHoldRdKey) {
            if (keyCode == 1) {
                holdRdKeyCode = 0;
                listeningHoldRdKey = false;
                return;
            }
            if (keyCode > 0) {
                holdRdKeyCode = keyCode;
                listeningHoldRdKey = false;
            }
            return;
        }
        if (listeningNickQolKey) {
            if (keyCode == 1) {
                hkNickQol = 0;
                listeningNickQolKey = false;
                return;
            }
            if (keyCode > 0) {
                hkNickQol = keyCode;
                listeningNickQolKey = false;
            }
            return;
        }
        if (listeningQuickbuyIndex >= 0) {
            if (keyCode == 1) {
                quickbuyKeys[listeningQuickbuyIndex] = 0;
                listeningQuickbuyIndex = -1;
                syncQuickbuyBinds();
                return;
            }
            if (keyCode > 0) {
                quickbuyKeys[listeningQuickbuyIndex] = keyCode;
                listeningQuickbuyIndex = -1;
                syncQuickbuyBinds();
            }
            return;
        }

        if (keyCode == 1) {
            activeInput = 0;
            closeScreenSafe();
            return;
        }

        boolean settingsInput = (selectedTheme == 0 && (activeInput >= 1 && activeInput <= 3));
        boolean utilsInput = (selectedTheme == 2 && activeInput == 4);
        boolean chatOverlayInput = (selectedTheme == 4 && (activeInput >= 5 && activeInput <= 7));
        if ((!settingsInput && !utilsInput && !chatOverlayInput) || activeInput == 0) return;
        if (keyCode == 28) {
            activeInput = 0;
            return;
        }

        String target = "";
        if (activeInput == 1) target = api_hypixel;
        else if (activeInput == 2) target = api_seraph;
        else if (activeInput == 3) target = api_aurora;
        else if (activeInput == 4) target = gl_message;
        else if (activeInput == 5) target = chatOverlayMinStars;
        else if (activeInput == 6) target = chatOverlayMinFkdr;
        else if (activeInput == 7) target = chatOverlayMinSwKdr;

        if (keyCode == 14) {
            if (target.length() > 0) target = target.substring(0, target.length() - 1);
        } else if ((Keyboard.isKeyDown(Keyboard.KEY_LCONTROL) || Keyboard.isKeyDown(Keyboard.KEY_RCONTROL)) && (keyCode == Keyboard.KEY_V)) {
            try {
                String clip = java.awt.Toolkit.getDefaultToolkit().getSystemClipboard().getData(java.awt.datatransfer.DataFlavor.stringFlavor).toString();
                if (clip != null) {
                    clip = clip.replace("\n", "").replace("\r", "");
                    target += clip;
                }
            } catch (Throwable ignored) {
            }
        } else if (typedChar >= 32 && typedChar <= 126 || typedChar == '.' || typedChar == '-') {
            if (activeInput >= 5 && activeInput <= 7) {
                if ((typedChar >= '0' && typedChar <= '9') || typedChar == '.' || (typedChar == '-' && target.length() == 0)) {
                    target += typedChar;
                }
            } else if (typedChar >= 32 && typedChar <= 126) {
                target += typedChar;
            }
        }

        if (target.length() > 128) {
            target = target.substring(0, 128);
        }
        if (activeInput == 1) api_hypixel = target;
        else if (activeInput == 2) api_seraph = target;
        else if (activeInput == 3) api_aurora = target;
        else if (activeInput == 4) gl_message = target;
        else if (activeInput == 5) chatOverlayMinStars = target;
        else if (activeInput == 6) chatOverlayMinFkdr = target;
        else if (activeInput == 7) chatOverlayMinSwKdr = target;
    }

    public void drawScreen(int mouseX, int mouseY, float f) {

        if (draggingHitboxSlider && selectedTheme == 2 && coloredHitboxes && Mouse.isButtonDown(0)) {
            hitboxThickness = hitboxThicknessFromMouse(hitboxSliderTrackX, hitboxSliderTrackW, mouseX);
        } else if (!Mouse.isButtonDown(0)) {
            draggingHitboxSlider = false;
        }

        int cfgTextPrimary = CLR_TEXT_PRIMARY;
        int cfgTextMuted = CLR_TEXT_MUTED;
        int cfgTextAccent = CLR_ACCENT;
        int frameBg = CLR_FRAME;
        int sidebarBg = CLR_SIDEBAR;
        int contentBg = CLR_CONTENT;
        int footerBg = CLR_FOOTER;
        int headerBg = CLR_HEADER;
        int headerLine = CLR_BORDER;

        this.drawDefaultBackground();
        Gui.drawRect(winX, winY + HEADER_H, winX + winW, winY + winH, frameBg);

        int leftX = winX + PANEL_INSET;
        int leftY = winY + HEADER_H + PANEL_INSET;
        int leftW = SIDEBAR_W;
        int leftH = winH - HEADER_H - FOOTER_H - (PANEL_INSET * 2);
        Gui.drawRect(leftX, leftY, leftX + leftW, leftY + leftH, sidebarBg);

        for (int i = 0; i < THEMES.length; i++) {
            int y = leftY + (i * 28) + themeScroll;
            if (y + 24 < leftY || y > leftY + leftH) continue;
            drawThemeItem(leftX + 4, y, leftW - 8, 24, THEMES[i], i == selectedTheme, mouseX, mouseY);
        }

        int contentX = leftX + leftW + CONTENT_GAP;
        int contentY = leftY + 5;
        int panelRight = winX + winW - PANEL_INSET;
        int scrollbarX = panelRight - SCROLLBAR_W - SCROLLBAR_GAP;
        int contentW = scrollbarX - CONTENT_INNER_GAP - contentX;
        Gui.drawRect(contentX - 4, leftY, panelRight, leftY + leftH, contentBg);
        Gui.drawRect(contentX - 4, leftY + 1, panelRight, leftY + 2, 0xFF1A1A1A);

        int viewportTop = leftY;
        int viewportBottom = leftY + leftH;
        int clipBottom = winY + winH - FOOTER_H;
        int viewportH = viewportBottom - viewportTop;
        int offset = getPanelScroll(selectedTheme);
        int cy = contentY + offset;

        int scissorRight = scrollbarX - SCROLLBAR_GAP;
        if (selectedTheme == 0) {
            drawSettingsApiPanel(contentX, cy, contentW, viewportTop, viewportBottom, mouseX, mouseY, cfgTextAccent, cfgTextMuted);
            drawContentViewportMask(contentX, scissorRight, viewportTop, viewportBottom, clipBottom, contentBg);
            drawScrollBar(scrollbarX, viewportTop + 2, viewportH - 4, getPanelContentHeight(0), settingsScroll);
        } else if (selectedTheme == 1) {
            drawAnticheatPanels(contentX, cy, contentW, viewportTop, viewportBottom, mouseX, mouseY, cfgTextAccent, cfgTextMuted);
            drawContentViewportMask(contentX, scissorRight, viewportTop, viewportBottom, clipBottom, contentBg);
            drawScrollBar(scrollbarX, viewportTop + 2, viewportH - 4, getPanelContentHeight(1), anticheatScroll);
        } else if (selectedTheme == 2) {
            drawUtilsPanels(contentX, cy, contentW, viewportTop, viewportBottom, mouseX, mouseY, cfgTextAccent, cfgTextMuted);
            drawContentViewportMask(contentX, scissorRight, viewportTop, viewportBottom, clipBottom, contentBg);
            drawScrollBar(scrollbarX, viewportTop + 2, viewportH - 4, getPanelContentHeight(2), utilsScroll);
        } else if (selectedTheme == 3) {
            int cyFixed = contentY;
            int cyList = contentY + OVERLAY_FIXED_HEADER_H + offset;
            int listTop = contentY + OVERLAY_FIXED_HEADER_H;
            if (listTop < viewportTop) listTop = viewportTop;
            int fixedBottom = contentY + OVERLAY_FIXED_HEADER_H;

            if (isRowVisible(cyList, 14, listTop, viewportBottom)) {
                drawTextLeft("Click row: enable/disable | Arrows: move", contentX, cyList, cfgTextMuted);
            }

            int[] sorted = getOverlaySortedIndexes();
            int rowY = cyList + 20;
            int rowH = 20;
            for (int pos = 0; pos < sorted.length; pos++) {
                int idx = sorted[pos];
                int y = rowY + (pos * 24);
                if (!isRowVisible(y, rowH, listTop, viewportBottom)) continue;

                int rowBg = CLR_ROW;
                if (!overlayColumnEnabled[idx]) rowBg = CLR_FIELD;
                if (overlaySelectedIndex == idx) rowBg = CLR_ROW_SELECTED;
                if (isHoveredRect(contentX, y, contentW - 36, rowH, mouseX, mouseY)) rowBg = CLR_ROW_HOVER;
                Gui.drawRect(contentX, y, contentX + contentW - 36, y + rowH, rowBg);

                int textColor = overlayColumnEnabled[idx] ? CLR_ON : CLR_OFF;
                drawTextLeft(columnLabelsForMode(overlayGamemodeEdit)[idx], contentX + 8, y + 6, textColor);

                int upX = contentX + contentW - 34;
                int downX = contentX + contentW - 17;
                int upBg = (pos == 0) ? CLR_FIELD : CLR_CARD_EDGE;
                int downBg = (pos == sorted.length - 1) ? CLR_FIELD : CLR_CARD_EDGE;
                Gui.drawRect(upX, y, upX + 14, y + rowH, upBg);
                Gui.drawRect(downX, y, downX + 14, y + rowH, downBg);
                drawTextLeft("^", upX + 4, y + 6, (pos == 0) ? 0xFF6F6F6F : cfgTextAccent);
                drawTextLeft("v", downX + 4, y + 6, (pos == sorted.length - 1) ? 0xFF6F6F6F : cfgTextAccent);
            }

            int sepRight = scrollbarX - SCROLLBAR_GAP;
            Gui.drawRect(contentX - 4, viewportTop, sepRight, fixedBottom, contentBg);
            Gui.drawRect(contentX - 4, fixedBottom - 1, sepRight, fixedBottom, CLR_BORDER);

            drawOverlayHeaderCard(contentX, cyFixed, contentW, viewportTop, viewportBottom, mouseX, mouseY, cfgTextAccent, cfgTextMuted);

            drawScrollBar(scrollbarX, viewportTop + 2, viewportH - 4, getPanelContentHeight(3), overlayScroll);
        } else if (selectedTheme == 4) {
            drawChatOverlayPanels(contentX, cy, contentW, viewportTop, viewportBottom, mouseX, mouseY, cfgTextAccent, cfgTextMuted);
            drawContentViewportMask(contentX, scissorRight, viewportTop, viewportBottom, clipBottom, contentBg);
            drawScrollBar(scrollbarX, viewportTop + 2, viewportH - 4, getPanelContentHeight(4), chatOverlayScroll);
        } else if (selectedTheme == 5) {
            drawSessionStatsPanels(contentX, cy, contentW, viewportTop, viewportBottom, mouseX, mouseY, cfgTextAccent, cfgTextMuted);
            drawContentViewportMask(contentX, scissorRight, viewportTop, viewportBottom, clipBottom, contentBg);
            drawScrollBar(scrollbarX, viewportTop + 2, viewportH - 4, getPanelContentHeight(5), sessionStatsScroll);
        } else if (selectedTheme == 6) {
            drawExperimentalPanels(contentX, cy, contentW, viewportTop, viewportBottom, mouseX, mouseY, cfgTextAccent, cfgTextMuted);
            drawContentViewportMask(contentX, scissorRight, viewportTop, viewportBottom, clipBottom, contentBg);
            drawScrollBar(scrollbarX, viewportTop + 2, viewportH - 4, getPanelContentHeight(6), experimentalScroll);
        } else if (selectedTheme == 7) {
            drawBlackchifyPanels(contentX, cy, contentW, viewportTop, viewportBottom, mouseX, mouseY, cfgTextAccent, cfgTextMuted);
            drawContentViewportMask(contentX, scissorRight, viewportTop, viewportBottom, clipBottom, contentBg);
            drawScrollBar(scrollbarX, viewportTop + 2, viewportH - 4, getPanelContentHeight(7), blackchifyScroll);
        }

        Gui.drawRect(winX, winY + winH - FOOTER_H, winX + winW, winY + winH, footerBg);

        Gui.drawRect(winX, winY, winX + winW, winY + HEADER_H, headerBg);
        Gui.drawRect(winX, winY + HEADER_H - 1, winX + winW, winY + HEADER_H, headerLine);
        Gui.drawRect(winX + 8, winY + 7, winX + 12, winY + 17, CLR_ON);
        drawTextLeft("Vigil / Settings", winX + 16, winY + 8, cfgTextAccent);
        drawTextLeft("LUNAR", winX + winW - 42, winY + 8, cfgTextMuted);

        super.drawScreen(mouseX, mouseY, f);
    }

    static {
        overlayOpacity = normalizeOpacity(overlayOpacity);
        hitboxThickness = normalizeHitboxThickness(hitboxThickness);
        overlayThemeColor = sanitizeHexColor(overlayThemeColor);
        if (overlayThemeColor.length() == 0) overlayThemeColor = "0F1316";
        configThemeColor = sanitizeHexColor(configThemeColor);
        if (configThemeColor.length() == 0) configThemeColor = "1A2227";
        configTextColor = sanitizeHexColor(configTextColor);
        if (configTextColor.length() == 0) configTextColor = "D6DEE4";
        if (overlayResourceLocation == null || (!"scoreboard".equals(overlayResourceLocation) && !"tab".equals(overlayResourceLocation))) {
            overlayResourceLocation = "tab";
        }
        sessionStatsBgOpacity = clampSessionOpacity(sessionStatsBgOpacity);
        sessionStatsScale = clampSessionScale(sessionStatsScale);
        sessionStatsPosX = clampSessionPos(sessionStatsPosX);
        sessionStatsPosY = clampSessionPos(sessionStatsPosY);
        tradeIndicatorPosX = clampSessionPos(tradeIndicatorPosX);
        tradeIndicatorPosY = clampSessionPos(tradeIndicatorPosY);
        loadOverlayColumnsForEditMode();
    }
}
