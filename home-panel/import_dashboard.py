# SPDX-License-Identifier: GPL-2.0-or-later
"""Read a Lovelace view and normalize supported cards. Never modifies HA."""
import argparse
import json
import unicodedata
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import unquote, urlsplit, urlunsplit
import websocket

ROOT = Path(__file__).resolve().parent


def parse_url(url):
    parts = urlsplit(url)
    if parts.scheme not in ("http", "https") or not parts.hostname or parts.username or parts.password:
        raise ValueError("Use an HTTP(S) dashboard URL without embedded credentials")
    # This first version supports HA hosted at the root, not reverse-proxy subpaths.
    path = [unquote(p) for p in parts.path.split("/") if p]
    if len(path) != 2:
        raise ValueError("Expected /dashboard-name/view-path (root-hosted HA)")
    if any("/" in p or p in (".", "..") for p in path):
        raise ValueError("Invalid dashboard or view path")
    origin = urlunsplit((parts.scheme, parts.netloc, "", "", ""))
    return origin, path[0], path[1]


def normalize(config, view_path, states):
    """Keep order and entity bindings; report unsupported content explicitly."""
    report = {"warnings": [], "tiles": [], "view": view_path, "ready": False}
    if "strategy" in config:
        report["warnings"].append("Automatic dashboard strategy is unsupported: " + str(config["strategy"].get("type", "unknown")))
        return report
    views = config.get("views", [])
    view = next((v for i, v in enumerate(views) if str(v.get("path", i)) == view_path), None)
    if view is None:
        report["warnings"].append("View not found: " + view_path)
        return report
    if "strategy" in view:
        report["warnings"].append("Automatic view strategy is unsupported")
        return report
    report["title"] = view.get("title", config.get("title", "Home Panel"))
    index = {s["entity_id"]: s for s in states}

    def add(entity, card, location):
        if not isinstance(entity, str) or "." not in entity:
            report["warnings"].append(location + ": missing or invalid entity")
            return
        state = index.get(entity, {})
        attrs = state.get("attributes", {})
        item = {"entity": entity, "name": card.get("name") or attrs.get("friendly_name") or entity,
                "state": state.get("state", "unavailable"), "unit": attrs.get("unit_of_measurement", ""),
                "card_type": card.get("type", "entity"), "read_only": True}
        for key in ("tap_action", "hold_action", "double_tap_action", "features", "visibility"):
            if key in card:
                report["warnings"].append(location + ": " + key + " is not implemented; tile is read-only")
        if entity not in index:
            report["warnings"].append(location + ": entity not available: " + entity)
        report["tiles"].append(item)

    def walk(card, location):
        if not isinstance(card, dict):
            report["warnings"].append(location + ": invalid card")
            return
        kind = card.get("type", "unknown")
        # Conditions/visibility must not silently become visible or actionable.
        if "visibility" in card:
            report["warnings"].append(location + ": visibility rules unsupported; card skipped")
            return
        if kind in ("grid", "vertical-stack", "horizontal-stack"):
            for i, child in enumerate(card.get("cards", [])):
                walk(child, location + "/" + str(i))
        elif kind == "heading":
            return  # Decorative headings do not become entity tiles.
        elif kind in ("sensor", "entity", "tile", "button"):
            add(card.get("entity"), card, location)
        elif kind in ("entities", "glance"):
            for i, row in enumerate(card.get("entities", [])):
                row = {"entity": row} if isinstance(row, str) else row
                if not isinstance(row, dict) or row.get("type") or row.get("visibility"):
                    report["warnings"].append(location + "/" + str(i) + ": special entity row unsupported")
                    continue
                add(row.get("entity"), row, location + "/" + str(i))
        else:
            report["warnings"].append(location + ": unsupported card type: " + str(kind))

    if view.get("badges"):
        report["warnings"].append("View badges are not imported")
    for i, card in enumerate(view.get("cards", [])):
        walk(card, "cards/" + str(i))
    for i, section in enumerate(view.get("sections", [])):
        if section.get("visibility"):
            report["warnings"].append("sections/" + str(i) + ": visibility rules unsupported; section skipped")
            continue
        for j, card in enumerate(section.get("cards", [])):
            walk(card, "sections/" + str(i) + "/" + str(j))
    report["ready"] = bool(report["tiles"])
    if not report["ready"]:
        report["warnings"].append("No supported entity cards found")
    return report


def display_tsv(report):
    """Bounded ASCII snapshot consumed by the small native renderer, no secrets."""
    def clean(value, limit):
        value = str(value).replace("?", "ss")
        value = unicodedata.normalize("NFKD", value).encode("ascii", "replace").decode()
        return "".join(c if 32 <= ord(c) < 127 else " " for c in value)[:limit]
    if len(report["tiles"]) > 64:
        raise ValueError("Dashboard exceeds the native renderer limit of 64 tiles")
    title = report.get("title") or report.get("dashboard", "Home Panel")
    lines = ["HOME_PANEL_V1", clean(title, 80), report["captured_at"]]
    for tile in report["tiles"]:
        lines.append("\t".join((clean(tile["name"], 80), clean(tile["state"], 80),
                               clean(tile["unit"], 30), clean(tile["entity"], 120))))
    return "\n".join(lines) + "\n"


def fetch(origin, dashboard, token):
    parts = urlsplit(origin)
    url = urlunsplit(("wss" if parts.scheme == "https" else "ws", parts.netloc, "/api/websocket", "", ""))
    ws = websocket.create_connection(url, timeout=15)
    try:
        if json.loads(ws.recv()).get("type") != "auth_required":
            raise ValueError("Unexpected authentication handshake")
        ws.send(json.dumps({"type": "auth", "access_token": token}))
        if json.loads(ws.recv()).get("type") != "auth_ok":
            raise ValueError("Home Assistant authentication failed")

        def request(number, kind, **fields):
            ws.send(json.dumps({"id": number, "type": kind, **fields}))
            while True:
                message = json.loads(ws.recv())
                if message.get("id") == number and message.get("type") == "result":
                    if not message.get("success"):
                        raise ValueError("HA request failed: " + str(message.get("error", {}).get("code", "unknown")))
                    return message["result"]
        config = request(1, "lovelace/config", url_path=None if dashboard == "lovelace" else dashboard)
        states = request(2, "get_states")
        return config, states
    finally:
        ws.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--secrets", type=Path, default=ROOT / "secrets.json")
    parser.add_argument("--dashboard-url")
    parser.add_argument("--out", type=Path, default=ROOT / "build/dashboard.json")
    args = parser.parse_args()
    try:
        secrets = json.loads(args.secrets.read_text(encoding="utf-8-sig"))
        url = args.dashboard_url or secrets.get("dashboard_url", "")
        origin, dashboard, view = parse_url(url)
        old_url = secrets.get("home_assistant_url")
        if old_url:
            old = urlsplit(old_url)
            new = urlsplit(origin)
            if (old.scheme, old.hostname, old.port) != (new.scheme, new.hostname, new.port):
                raise ValueError("Dashboard origin differs from the configured HA server; check the URL before sending the token")
        token = secrets.get("home_assistant_token", "").strip()
        if not token:
            raise ValueError("Missing Home Assistant token")
        config, states = fetch(origin, dashboard, token)
        report = normalize(config, view, states)
        report["dashboard"] = dashboard
        report["snapshot_only"] = True
        report["captured_at"] = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
        report["warnings"].append("This host-side import is a read-only snapshot; on-device synchronization is not implemented yet")
        args.out.parent.mkdir(parents=True, exist_ok=True)
        report_path = args.out.with_name(args.out.stem + "-report.json")
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        for warning in report["warnings"]:
            print("NOTE:", warning)
        if not report["ready"]:
            print("No usable view imported. Existing output has been preserved.")
            return 2
        tsv = display_tsv(report)
        temp = args.out.with_suffix(args.out.suffix + ".tmp")
        temp.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        temp.replace(args.out)
        tsv_path = args.out.with_suffix(".tsv")
        tsv_temp = tsv_path.with_suffix(".tsv.tmp")
        tsv_temp.write_text(tsv, encoding="ascii", newline="\n")
        tsv_temp.replace(tsv_path)
        print("Imported", len(report["tiles"]), "read-only tiles to", args.out)
        return 0
    except (ValueError, OSError, websocket.WebSocketException) as exc:
        # Never print a transport exception that might include headers or credentials.
        print("Import failed:", str(exc) if isinstance(exc, ValueError) else type(exc).__name__)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
