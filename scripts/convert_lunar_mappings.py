#!/usr/bin/env python3
"""Convert vanilla obfuscated JNI names to Lunar/MCP names in a target src/ folder."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

PARENT = Path(__file__).resolve().parents[1]

# obf internal name -> slash-separated MCP path
CLASS_OBF_TO_MCP: dict[str, str] = {}
FIELD_OBF_TO_MCP: dict[tuple[str, str], str] = {}
METHOD_OBF_TO_MCP: dict[tuple[str, str, str], str] = {}
SRG_FIELD_TO_NAME: dict[str, str] = {}
SRG_METHOD_TO_NAME: dict[str, str] = {}

KNOWN_VAR_OWNER: dict[str, str] = {
    "mcClass": "ave",
    "aveClass": "ave",
    "g_mcClassGlobal": "ave",
    "clsMinecraft": "ave",
    "localMc": "ave",
    "guiChatClass": "awv",
    "guiChestClass": "ayr",
    "guiContainerClass": "ayl",
    "scorePlayerTeamClass": "aul",
    "serClass": "eu$a",
    "localScaled": "avr",
    "localGui": "avp",
    "localGuiIngame": "avo",
    "localTabOverlay": "awh",
    "localIChatComponent": "eu",
    "localFr": "avn",
    "localTm": "bmj",
    "localNhp": "bcy",
    "localNpi": "bdc",
    "clsPlayerSp": "bew",
    "clsScaledResolution": "avr",
    "clsGui": "avp",
    "clsGuiIngame": "avo",
    "clsPlayerTabOverlay": "awh",
    "clsIChatComponent": "eu",
    "clsFontRenderer": "avn",
    "clsTextureManager": "bmj",
    "clsNetHandlerPlayClient": "bcy",
    "clsNetworkPlayerInfo": "bdc",
    "aabbClass": "aug",
    "rmClass": "biu",
    "entityBase": "pk",
    "pkClass": "pk",
    "baseEntityClass": "pk",
    "entityBaseClass": "pk",
    "livingClass": "pr",
    "dwClass": "pz",
    "playerClass": "bew",
    "clientPlayerClass": "bet",
    "clsEntityPlayer": "wn",
    "clsAbstractClient": "bet",
    "smClass": "bnp",
    "cctClass": "fa",
    "fmtClass": "a",
    "enumClass": "a",
    "ezClass": "ez",
    "euClass": "eu",
    "chatClass": "eu",
    "textClass": "fa",
    "styleClass": "ez",
    "ingameGuiClass": "avo",
    "chatGuiClass": "avt",
    "chatGuiClass2": "avt",
    "guiTextFieldClass": "avw",
    "scoreboardClass": "auo",
    "objectiveClass": "auk",
    "chatLineClass": "awd",
    "componentClass": "eu",
    "invClass": "og",
    "lowerInvClass": "og",
    "worldClass": "adm",
    "worldClientClass": "bdb",
    "bbClass": "aug",
    "entityClass": "pk",
    "selfClass": "bew",
    "stackClass": "zx",
    "invClass": "xi",
    "slotClass": "yg",
    "containerClass": "xi",
    "pcClass": "bda",
    "nhClass": "bcy",
    "infoClass": "bdc",
    "sbClass": "auo",
    "teamClass": "aul",
    "compClass": "eu",
    "cc": "eu",
    "scoreClass": "aum",
    "timerClass": "avl",
    "shClass": "bpz",
}

# C++ -> generated *Lunar.java method names (Java rename when type goes MCP).
EXPLICIT_METHOD_RENAMES: dict[str, str] = {
    "copyBfrFields": "copyRenderGlobalFields",
    "copyAvoFields": "copyGuiIngameFields",
}

# Vanilla obf field names that must not use the MCP csv name on this owner.
EXPLICIT_FIELD_OBF_TO_MCP: dict[tuple[str, str], str] = {
    ("pk", "ae"): "ridingEntity",
    ("pk", "m"): "ridingEntity",
    ("pr", "ar"): "isSwingInProgress",
    ("pr", "as"): "swingProgressInt",
}

JNI_LOADER_FILES = {
    "dllmain.cpp",
    "tab_overlay.cpp",
    "colored_hitboxes.cpp",
    "jni_helpers.cpp",
    "utils.cpp",
    "anticheat.cpp",
    "denick.cpp",
    "session_stats.cpp",
}


def load_mappings(mcp_root: Path) -> None:
    srg = mcp_root / "mcp" / "joined.srg"
    fields_csv = mcp_root / "mcp_csv" / "fields.csv"
    methods_csv = mcp_root / "mcp_csv" / "methods.csv"

    with open(fields_csv, encoding="utf-8") as f:
        next(f)
        for line in f:
            parts = line.strip().split(",", 3)
            if len(parts) >= 2:
                SRG_FIELD_TO_NAME[parts[0]] = parts[1]

    with open(methods_csv, encoding="utf-8") as f:
        next(f)
        for line in f:
            parts = line.strip().split(",", 3)
            if len(parts) >= 2:
                SRG_METHOD_TO_NAME[parts[0]] = parts[1]

    with open(srg, encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if line.startswith("CL: "):
                _, obf, mcp = line.split(" ", 2)
                CLASS_OBF_TO_MCP[obf] = mcp
            elif line.startswith("FD: "):
                _, key, srg_path = line.split(" ", 2)
                owner, member = key.split("/", 1)
                srg = srg_path.rsplit("/", 1)[-1]
                human = SRG_FIELD_TO_NAME.get(srg, member)
                FIELD_OBF_TO_MCP[(owner, member)] = human
            elif line.startswith("MD: "):
                parts = line.split(" ")
                if len(parts) >= 4:
                    owner_member = parts[1]
                    sig = parts[2]
                    srg = parts[3].rsplit("/", 1)[-1]
                    owner, member = owner_member.split("/", 1)
                    human = SRG_METHOD_TO_NAME.get(srg, member)
                    METHOD_OBF_TO_MCP[(owner, member, sig)] = human


def mcp_to_jni_path(mcp: str) -> str:
    return mcp.replace(".", "/")


def mcp_to_dot(mcp: str) -> str:
    return mcp.replace("/", ".")


def translate_sig(sig: str) -> str:
    if not sig:
        return sig

    def repl(m: re.Match[str]) -> str:
        obf = m.group(1)
        if obf in CLASS_OBF_TO_MCP:
            return "L" + mcp_to_jni_path(CLASS_OBF_TO_MCP[obf]) + ";"
        return m.group(0)

    return re.sub(r"L([^;]+);", repl, sig)


def is_game_obf(name: str) -> bool:
    return name in CLASS_OBF_TO_MCP


def convert_findclass(line: str) -> str:
    def repl(m: re.Match[str]) -> str:
        path = m.group(1)
        if path.startswith("java/"):
            return m.group(0)
        if is_game_obf(path):
            mcp = CLASS_OBF_TO_MCP[path]
            return f'McFindClass(env, "{mcp_to_jni_path(mcp)}")'
        # authlib / MCP paths need Lunar ClassLoader (FindClass returns null).
        if path.startswith("net/") or path.startswith("com/"):
            return f'McFindClass(env, "{path}")'
        return m.group(0)

    return re.sub(r'env->FindClass\("([^"]+)"\)', repl, line)


def convert_load_class_via_mc(line: str) -> str:
    def repl(m: re.Match[str]) -> str:
        obf = m.group(1)
        if not is_game_obf(obf):
            return m.group(0)
        mcp = CLASS_OBF_TO_MCP[obf]
        return f'LoadClassViaMc(env, mcObj, "{mcp_to_dot(mcp)}")'

    return re.sub(
        r'LoadClassViaMc\(env,\s*mcObj,\s*"([^"]+)"\)',
        repl,
        line,
    )


def lookup_field_mcp(owner: str | None, name: str) -> str | None:
    if owner:
        explicit = EXPLICIT_FIELD_OBF_TO_MCP.get((owner, name))
        if explicit:
            return explicit
        hit = FIELD_OBF_TO_MCP.get((owner, name))
        if hit:
            return hit
    matches = {v for (o, n), v in FIELD_OBF_TO_MCP.items() if n == name}
    if len(matches) == 1:
        return next(iter(matches))
    return None


def deobf_signature(sig: str) -> str:
    if not sig or "Lnet/minecraft/" in sig or "Lcom/mojang/" in sig:
        return sig
    out = sig
    for obf, mcp in CLASS_OBF_TO_MCP.items():
        out = out.replace(f"L{obf};", f"L{mcp_to_jni_path(mcp)};")
    return out


def lookup_method_mcp(owner: str | None, name: str, sig: str) -> str | None:
    sigs = [sig]
    deobf = deobf_signature(sig)
    if deobf != sig:
        sigs.append(deobf)
    for candidate in sigs:
        if owner:
            hit = METHOD_OBF_TO_MCP.get((owner, name, candidate))
            if hit:
                return hit
        matches = {v for (o, n, s), v in METHOD_OBF_TO_MCP.items() if n == name and s == candidate}
        if len(matches) == 1:
            return next(iter(matches))
    return None


def track_var_owner(line: str, var_owner: dict[str, str]) -> None:
    m = re.search(
        r"jclass\s+(\w+)\s*=\s*(?:\(jclass\)\s*)?(?:env->)?(?:McFindClass|FindClass)\(env,\s*\"([^\"]+)\"\)",
        line,
    )
    if m:
        var, path = m.group(1), m.group(2)
        for obf, mcp in CLASS_OBF_TO_MCP.items():
            if mcp_to_jni_path(mcp) == path:
                var_owner[var] = obf
                return
    m = re.search(r"jclass\s+(\w+)\s*=\s*env->FindClass\(\"([^\"]+)\"\)", line)
    if m:
        var, obf = m.group(1), m.group(2)
        if is_game_obf(obf):
            var_owner[var] = obf
    m = re.search(r"jclass\s+(\w+)\s*=\s*\(jclass\)env->NewGlobalRef\((\w+)\)", line)
    if m:
        dst, src = m.group(1), m.group(2)
        if src in var_owner:
            var_owner[dst] = var_owner[src]


def resolve_owner(var: str, var_owner: dict[str, str]) -> str | None:
    if var in var_owner:
        return var_owner[var]
    if var in KNOWN_VAR_OWNER:
        return KNOWN_VAR_OWNER[var]
    if var.startswith("cls") and var.endswith("Minecraft"):
        return "ave"
    return None


def convert_member_access(line: str, var_owner: dict[str, str]) -> str:
    for kind, pattern in (
        ("field", r"env->GetFieldID\((\w+),\s*\"([^\"]+)\",\s*\"([^\"]+)\"\)"),
        ("static_field", r"env->GetStaticFieldID\((\w+),\s*\"([^\"]+)\",\s*\"([^\"]+)\"\)"),
        ("method", r"env->GetMethodID\((\w+),\s*\"([^\"]+)\",\s*\"([^\"]+)\"\)"),
        ("static_method", r"env->GetStaticMethodID\((\w+),\s*\"([^\"]+)\",\s*\"([^\"]+)\"\)"),
    ):
        def repl(m: re.Match[str]) -> str:
            var, name, sig = m.group(1), m.group(2), m.group(3)
            if name == "<init>":
                new_sig = translate_sig(sig)
                if new_sig != sig:
                    return m.group(0).replace(f'"{sig}"', f'"{new_sig}"')
                return m.group(0)
            owner = resolve_owner(var, var_owner)
            if not owner:
                new_sig = translate_sig(sig)
                if new_sig != sig:
                    return m.group(0).replace(f'"{sig}"', f'"{new_sig}"')
                return m.group(0)
            if kind in ("field", "static_field"):
                new_name = lookup_field_mcp(owner, name)
            else:
                new_name = lookup_method_mcp(owner, name, sig)
            new_sig = translate_sig(sig)
            if kind in ("method", "static_method") and name in EXPLICIT_METHOD_RENAMES:
                new_name = EXPLICIT_METHOD_RENAMES[name]
            out = m.group(0)
            if new_name and new_name != name:
                out = out.replace(f'"{name}"', f'"{new_name}"', 1)
            if new_sig != sig:
                out = out.replace(f'"{sig}"', f'"{new_sig}"', 1)
            return out

        line = re.sub(pattern, repl, line, flags=re.DOTALL)
    return line


def translate_sig_literals(line: str) -> str:
    return re.sub(r'"([^"]*L[a-zA-Z0-9_$]+;[^"]*)"', lambda m: f'"{translate_sig(m.group(1))}"', line)


def ensure_jni_loader_include(text: str, filename: str) -> str:
    if filename not in JNI_LOADER_FILES:
        return text
    if "McFindClass" not in text:
        return text
    if '#include "class_finder.h"' in text:
        return text
    if '#include "jni_mc_loader.h"' in text:
        return text.replace('#include "jni_mc_loader.h"', '#include "class_finder.h"', 1)
    if '#include "globals.h"' in text:
        return text.replace(
            '#include "globals.h"',
            '#include "globals.h"\n#include "class_finder.h"',
            1,
        )
    lines = text.splitlines(keepends=True)
    if lines and lines[0].startswith("#include"):
        lines.insert(1, '#include "class_finder.h"\n')
        return "".join(lines)
    return '#include "class_finder.h"\n' + text


def apply_lunar_loader_order(text: str, rel_path: str) -> str:
    if rel_path.replace("\\", "/") != "dllmain.cpp":
        return text
    needle = (
        "        if (aveClass) env->DeleteLocalRef(aveClass);\n\n"
        "        if (!g_tabOverlay.initialize(env)) {"
    )
    loader_block = (
        "        if (loader && !g_injectClassLoaderGlobal) {\n"
        "            g_injectClassLoaderGlobal = env->NewGlobalRef(loader);\n"
        "        }\n\n"
    )
    if needle in text and loader_block not in text.split(needle)[0][-200:]:
        text = text.replace(needle, "        if (aveClass) env->DeleteLocalRef(aveClass);\n\n" + loader_block + "        if (!g_tabOverlay.initialize(env)) {", 1)
    return text


def apply_lunar_java_headers(text: str, rel_path: str) -> str:
    if rel_path.replace("\\", "/") == "globals.h":
        return text.replace(
            '#include "../java_src/FrenchifyGui.h"',
            '#include "../java_src/lunar_headers/FrenchifyGuiLunar.h"',
        )
    if rel_path.replace("\\", "/") == "colored_hitboxes.cpp":
        text = text.replace(
            '#include "../java_src/FrenchifyRenderGlobal.h"',
            '#include "../java_src/lunar_headers/FrenchifyRenderGlobalLunar.h"',
        )
        text = text.replace(
            '#include "../java_src/FrenchifyHitboxInstaller.h"',
            '#include "../java_src/lunar_headers/FrenchifyHitboxInstallerLunar.h"',
        )
        text = text.replace("FrenchifyRenderGlobal_class", "FrenchifyRenderGlobalLunar_class")
        text = text.replace("FrenchifyHitboxInstaller_class", "FrenchifyHitboxInstallerLunar_class")
    if rel_path.replace("\\", "/") == "dllmain.cpp":
        text = text.replace(
            '#include "../java_src/FrenchifyTabOverlayNative.h"',
            '#include "../java_src/lunar_headers/FrenchifyTabOverlayNativeLunar.h"',
        )
        text = text.replace("FrenchifyGui_class", "FrenchifyGuiLunar_class")
        text = text.replace("FrenchifyTabOverlayNative_class", "FrenchifyTabOverlayNativeLunar_class")
    if rel_path.replace("\\", "/") == "session_stats.cpp":
        text = text.replace(
            '#include "../java_src/FrenchifyIngameGui.h"',
            '#include "../java_src/lunar_headers/FrenchifyIngameGuiLunar.h"',
        )
        text = text.replace(
            '#include "../java_src/FrenchifySessionHud.h"',
            '#include "../java_src/lunar_headers/FrenchifySessionHudLunar.h"',
        )
        text = text.replace(
            '#include "../java_src/FrenchifySessionHudInstaller.h"',
            '#include "../java_src/lunar_headers/FrenchifySessionHudInstallerLunar.h"',
        )
        text = text.replace("FrenchifyIngameGui_class", "FrenchifyIngameGuiLunar_class")
        text = text.replace("FrenchifySessionHud_class", "FrenchifySessionHudLunar_class")
        text = text.replace("FrenchifySessionHudInstaller_class", "FrenchifySessionHudInstallerLunar_class")
    return text


def normalize_multiline_jni(text: str) -> str:
    """Join split GetFieldID / GetMethodID / GetStatic* calls onto one line."""
    patterns = (
        r"(env->Get(?:Static)?(?:Field|Method)ID\(\s*\w+\s*,\s*)\r?\n\s*\"([^\"]+)\"\s*,\s*\r?\n\s*\"([^\"]+)\"\s*\)",
    )
    for pattern in patterns:
        text = re.sub(pattern, r'\1"\2", "\3")', text)
    return text


def convert_file(path: Path) -> bool:
    text = path.read_text(encoding="utf-8")
    original = text
    text = normalize_multiline_jni(text)
    var_owner: dict[str, str] = dict(KNOWN_VAR_OWNER)
    out_lines: list[str] = []
    for line in text.splitlines(keepends=True):
        track_var_owner(line, var_owner)
        line = convert_findclass(line)
        line = convert_load_class_via_mc(line)
        line = convert_member_access(line, var_owner)
        line = translate_sig_literals(line)
        out_lines.append(line)
    new_text = "".join(out_lines)
    new_text = ensure_jni_loader_include(new_text, path.name)
    new_text = apply_lunar_java_headers(new_text, path.name)
    new_text = apply_lunar_loader_order(new_text, path.name)
    if new_text != original:
        path.write_text(new_text, encoding="utf-8")
        return True
    return False


def write_obf_map_header(mcp_root: Path, out_path: Path) -> None:
    load_mappings(mcp_root.resolve())
    rows: list[tuple[str, str]] = []
    for obf, mcp in CLASS_OBF_TO_MCP.items():
        rows.append((mcp.replace(".", "/"), obf))
    rows.sort(key=lambda x: x[0])

    lines = [
        "#pragma once",
        "",
        "struct ClassObfEntry {",
        "    const char* mcpSlash;",
        "    const char* obfSlash;",
        "};",
        "",
        f"static const ClassObfEntry kClassObfMap[] = {{",
    ]
    for mcp_slash, obf in rows:
        lines.append(f'    {{"{mcp_slash}", "{obf}"}},')
    lines.extend([
        "};",
        "",
        f"static const int kClassObfMapCount = {len(rows)};",
        "",
    ])
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {out_path.relative_to(PARENT)}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert vanilla JNI obf names to Lunar MCP")
    parser.add_argument(
        "--target",
        type=Path,
        default=PARENT / "lunar_test",
        help="Lunar build folder containing src/ (default: lunar_test)",
    )
    parser.add_argument(
        "--mcp-root",
        type=Path,
        default=PARENT,
        help="Folder with mcp/ and mcp_csv/ (default: project root)",
    )
    args = parser.parse_args()

    src_dir = args.target / "src"
    if not src_dir.is_dir():
        print(f"Missing src dir: {src_dir}")
        return 1

    load_mappings(args.mcp_root.resolve())
    platform_map = args.mcp_root.resolve() / "platform" / "lunar" / "class_finder_obf_map.h"
    write_obf_map_header(args.mcp_root.resolve(), platform_map)
    changed = 0
    for path in sorted(src_dir.glob("*.cpp")) + sorted(src_dir.glob("*.h")):
        if convert_file(path):
            print(f"updated: {path.name}")
            changed += 1
    print(f"Done. {changed} file(s) updated in {src_dir}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
