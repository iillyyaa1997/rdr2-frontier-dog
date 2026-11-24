#!/usr/bin/env python3
"""gen_lml_items.py CATALOG_SP.YMT OUT_DIR [--lang ru|en]

Builds the Lenny's Mod Loader folder "FrontierDogItems" with Frontier Dog's own items (D133, D138):
  CONSUMABLE_DOG_REVIVER  - sold by the stable hand (ST_HORSE_SHOP list pages, next to the Horse Reviver)
  CONSUMABLE_DOG_FOOD     - sold by the butcher (ST_BUTCHER list page)
  CONSUMABLE_DOG_TREAT    - sold by the butcher

The game reads ONE catalog_sp.ymt, so the input must be the catalog that is installed now (on the operator's PC:
red_dead_offline's copy); the output is that catalog plus our items. LML (verified 16.11): the folder must be listed in
lml\mods.xml BOTH under <Mods> and under <LoadOrder> (a folder missing from <LoadOrder> is parsed but never loaded), and
AFTER the mod the catalog came from - for one game path the later mod in <LoadOrder> wins ('Overwriting ... from').
Rebuild this folder whenever the source mod's catalog changes (update/removal of red_dead_offline). Item definitions are clones
of CONSUMABLE_HORSE_REVIVER's entry (a horse item: Arthur cannot eat it himself; the mod uses it through its prompts),
with our keys, prices and texts. Texts go to an LML text gxt2 ("0xHASH = text" lines, the format red_dead_offline uses).
Plain text processing, line by line: the catalog is a 57 MB XML file with CRLF line ends. The folder also carries the
dog's core icon, textures/frontier_dog_hud.ytd built by gen_ytd.py from data/hud, and the trader talk's paper, seal and
signature, textures/frontier_dog_ui.ytd from data/ui (needs Pillow).
"""
import os
import re
import sys

ITEMS = [
    # key, price cents, sell cents, inventory icon id, grid icon id, RU name, RU desc, EN name, EN desc
    ("CONSUMABLE_DOG_REVIVER", 950, 475, "CONSUMABLE_HORSE_REVIVER", "UI_CONSUMABLE_HORSE_REVIVER",
     "Оживитель для собак", "Ставит на ноги раненую собаку.",
     "Dog Reviver", "Gets a wounded dog back on its feet."),
    ("CONSUMABLE_DOG_FOOD", 75, 35, "CONSUMABLE_BISCUIT_BOX", "UI_CONSUMABLE_BISCUIT_BOX",
     "Корм для собак", "Сытная еда для собаки.",
     "Dog Food", "A filling meal for a dog."),
    ("CONSUMABLE_DOG_TREAT", 40, 20, "CONSUMABLE_BISCUIT_BOX", "UI_CONSUMABLE_BISCUIT_BOX",
     "Лакомство для собак", "Собака его обожает: немного укрепляет привязанность.",
     "Dog Treat", "Dogs love it: a little more bonding."),
]
STABLE_ITEMS = ["CONSUMABLE_DOG_REVIVER"]
BUTCHER_ITEMS = ["CONSUMABLE_DOG_FOOD", "CONSUMABLE_DOG_TREAT"]
STOCK = 10


def joaat(s):
    h = 0
    for c in s.lower().encode():
        h = (h + c) & 0xFFFFFFFF
        h = (h + (h << 10)) & 0xFFFFFFFF
        h ^= h >> 6
    h = (h + (h << 3)) & 0xFFFFFFFF
    h ^= h >> 11
    return (h + (h << 15)) & 0xFFFFFFFF


def section(lines, name):
    for i, l in enumerate(lines):
        if l == f"  <{name}>":
            for j in range(i + 1, len(lines)):
                if lines[j] == f"  </{name}>":
                    return i, j
    raise SystemExit(f"section {name} not found")


def block(lines, start, indent):
    """[start, end] of the element that opens at lines[start] with the given indent"""
    close = " " * indent + "</item>"
    for j in range(start + 1, len(lines)):
        if lines[j] == close:
            return start, j
    raise SystemExit(f"no end for block at {start + 1}")


def clone_item(lines, key, price, sell, icon, grid):
    c0, c1 = section(lines, "catalog")
    for i in range(c0, c1):
        if lines[i] == '      <item key="CONSUMABLE_HORSE_REVIVER">':
            s, e = block(lines, i, 6)
            src = lines[s:e + 1]
            break
    else:
        raise SystemExit("CONSUMABLE_HORSE_REVIVER entry not found in <catalog>")
    out, in_loc, in_acq, in_sell = [], False, False, False
    for l in src:
        l = l.replace("CONSUMABLE_HORSE_REVIVER_DESC", key + "_DESC")
        if "<id>UI_CONSUMABLE_HORSE_REVIVER</id>" in l:
            l = l.replace("UI_CONSUMABLE_HORSE_REVIVER", grid)
        elif "<id>CONSUMABLE_HORSE_REVIVER</id>" in l:
            l = l.replace("CONSUMABLE_HORSE_REVIVER", icon)
        else:
            l = l.replace("CONSUMABLE_HORSE_REVIVER", key)
        # the horse texts' brand / catalogue description labels: dropped (ours come from the gxt2)
        if l.strip() == "<localization>":
            in_loc = True
            out.append(l.replace("<localization>", "<localization />"))
            continue
        if in_loc:
            if l.strip() == "</localization>":
                in_loc = False
            continue
        if l.strip() == "<acquirecosts>":
            in_acq = True
        if l.strip() == "</acquirecosts>":
            in_acq = False
        if l.strip() == "<sellprices>":
            in_sell = True
        if l.strip() == "</sellprices>":
            in_sell = False
        m = re.match(r'^(\s*)<quantity value="(\d+)" />$', l)
        if m and (in_acq or in_sell) and m.group(2) not in ("1",):
            l = f'{m.group(1)}<quantity value="{price if in_acq else sell}" />'
        out.append(l)
    return out


def row(key, indent):
    p = " " * indent
    return [f"{p}<item>", f"{p}  <key>{key}</key>", f"{p}  <linkshopid />", f"{p}  <linkmenuid />", f"{p}</item>"]


def stock_entry(key):
    return ["        <item>", f"          <item>{key}</item>", "          <requirementgroups>", "            <item>",
            f'              <count value="{STOCK}" />', "              <requirements />", "            </item>",
            "          </requirementgroups>", "        </item>"]


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    src, out_dir = sys.argv[1], sys.argv[2]
    lang = sys.argv[sys.argv.index("--lang") + 1] if "--lang" in sys.argv else "ru"
    raw = open(src, "rb").read()
    crlf = b"\r\n" in raw[:4096]
    text = raw.decode("utf-8-sig")
    lines = text.split("\r\n" if crlf else "\n")
    keys = [it[0] for it in ITEMS]
    if any(f'<item key="{k}">' in text for k in keys):
        raise SystemExit("the input already has our items: give the catalog WITHOUT Frontier Dog items")

    inserts = []  # (index, lines) applied bottom-up

    # 1. item definitions at the end of <catalog><items>
    c0, c1 = section(lines, "catalog")
    end_items = max(i for i in range(c0, c1) if lines[i] == "    </items>")
    defs = []
    for key, price, sell, icon, grid, *_ in ITEMS:
        defs += clone_item(lines, key, price, sell, icon, grid)
    inserts.append((end_items, defs))

    # 2. menu rows: stable list pages after the Horse Reviver row, butcher list page after Prime Beef
    l0, l1 = section(lines, "cataloglayout")
    shoptype, rows_added = None, {"ST_HORSE_SHOP": 0, "ST_BUTCHER": 0}
    for i in range(l0, l1):
        m = re.match(r"^      <shoptype>([^<]+)</shoptype>$", lines[i])
        if m:
            shoptype = m.group(1)
        key = re.match(r"^(\s*)<key>([^<]+)</key>$", lines[i])
        if not key:
            continue
        ind = len(key.group(1)) - 2
        if shoptype == "ST_HORSE_SHOP" and key.group(2) == "CONSUMABLE_HORSE_REVIVER":
            _, e = block(lines, i - 1, ind)
            inserts.append((e + 1, [x for k in STABLE_ITEMS for x in row(k, ind)]))
            rows_added["ST_HORSE_SHOP"] += 1
        if shoptype == "ST_BUTCHER" and key.group(2) == "PROVISION_PRIME_BEEF":
            _, e = block(lines, i - 1, ind)
            inserts.append((e + 1, [x for k in BUTCHER_ITEMS for x in row(k, ind)]))
            rows_added["ST_BUTCHER"] += 1
    if not all(rows_added.values()):
        raise SystemExit(f"menu rows not placed: {rows_added}")

    # 3. stock: first in the shop types' item lists
    s0, s1 = section(lines, "shopsinventories")
    stocked = set()
    for i in range(s0, s1):
        m = re.match(r"^      <type>(ST_HORSE_SHOP|ST_BUTCHER)</type>$", lines[i])
        if m and lines[i + 1] == "      <items>":
            ks = STABLE_ITEMS if m.group(1) == "ST_HORSE_SHOP" else BUTCHER_ITEMS
            inserts.append((i + 2, [x for k in ks for x in stock_entry(k)]))
            stocked.add(m.group(1))
    if stocked != {"ST_HORSE_SHOP", "ST_BUTCHER"}:
        raise SystemExit(f"stock not placed: {stocked}")

    for idx, new in sorted(inserts, key=lambda t: t[0], reverse=True):
        lines[idx:idx] = new

    mod = os.path.join(out_dir, "FrontierDogItems")
    os.makedirs(os.path.join(mod, "catalog"), exist_ok=True)
    with open(os.path.join(mod, "catalog", "catalog_sp.ymt"), "wb") as f:
        f.write(("\r\n" if crlf else "\n").join(lines).encode("utf-8"))
    name_i, desc_i = (5, 6) if lang == "ru" else (7, 8)
    with open(os.path.join(mod, "strings.gxt2"), "wb") as f:
        for it in ITEMS:
            f.write(f"0x{joaat(it[0]):08X} = {it[name_i]}\r\n".encode("utf-8"))
            f.write(f"0x{joaat(it[0] + '_DESC'):08X} = {it[desc_i]}\r\n".encode("utf-8"))
    with open(os.path.join(mod, "install.xml"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("""<EasyInstall>
    <Name>Frontier Dog Items</Name>
    <Author>Frontier Dog</Author>
    <Version>0.1.0</Version>
    <Link></Link>
    <Metadata>
    </Metadata>
    <Resources>
        <Resource>
            <DataFile>strings.gxt2</DataFile>
        </Resource>
        <Resource>
            <!-- the catalog that was installed before + Frontier Dog's items; must load after the mod it was built from -->
            <FileReplacement>
                <GamePath>platform:/data/itemdatabase/catalog_sp.ymt</GamePath>
                <FilePath>catalog/catalog_sp.ymt</FilePath>
            </FileReplacement>
        </Resource>
        <Resource>
            <!-- the dog's core icon: textures/frontier_dog_hud.ytd (tools/gen_ytd.py from data/hud), an add-on
                 texture dictionary the mod draws with DRAW_SPRITE, streamed like Care Overhaul's rdr_care_icons.ytd -->
            <StreamingFiles isAddon="true">textures</StreamingFiles>
        </Resource>
    </Resources>
</EasyInstall>
""")
    # the dog's core icon (D148): data/hud/paw_core_0..15.png -> textures/frontier_dog_hud.ytd
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    import gen_ytd
    from PIL import Image
    hud = os.path.join(here, "..", "data", "hud")
    os.makedirs(os.path.join(mod, "textures"), exist_ok=True)
    with open(os.path.join(mod, "textures", "frontier_dog_hud.ytd"), "wb") as f:
        f.write(gen_ytd.build([(f"paw_core_{i}", Image.open(os.path.join(hud, f"paw_core_{i}.png")).convert("RGBA"))
                               for i in range(16)]))
    # the trader's talk (24.11): the paper cut from the game's map, the seal ring cut from its compass rose, the
    # trader's hand in the game's handwriting font -> textures/frontier_dog_ui.ytd
    ui = os.path.join(here, "..", "data", "ui")
    with open(os.path.join(mod, "textures", "frontier_dog_ui.ytd"), "wb") as f:
        f.write(gen_ytd.build([(n, Image.open(os.path.join(ui, n + ".png")).convert("RGBA")) for n in ("fd_sheet", "fd_seal", "fd_sign")]))
    print(f"items {len(ITEMS)}, stable rows {rows_added['ST_HORSE_SHOP']}, butcher rows {rows_added['ST_BUTCHER']}, "
          f"stock {sorted(stocked)}, lines {len(lines)}")


if __name__ == "__main__":
    main()
