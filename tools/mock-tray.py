#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A stand-in for the panel's tray (StatusNotifierWatcher) and for the
notification server, for testing the tray symbol without a desktop (needs
dbus-next: `pip install dbus-next` or the package python3-dbus-next):

    mkfifo /tmp/tray.in
    dbus-run-session -- sh -c 'tools/mock-tray.py < /tmp/tray.in & build/halcyon' &
    exec 3> /tmp/tray.in; echo "menu" >&3

Prints one JSON line per event: the registered item with its properties and
menu, every notification, every change the item reports (tool tip, menu).
Commands on stdin act like the panel: activate, secondary, scroll N,
click ID (menu entry), menu (print the menu), item (print the properties),
quit-watcher (the panel goes away), restart-watcher (and comes back).
--no-watcher starts without a tray (only notifications).
"""
import argparse, asyncio, json, sys
from dbus_next.aio import MessageBus
from dbus_next.service import ServiceInterface, method, signal, dbus_property, PropertyAccess
from dbus_next import Variant, Message, MessageType

args = None
bus = None
item = None  # (bus name, path)
owner = None  # unique name behind it (signals come from there)


def out(**kw):
    print(json.dumps(kw, ensure_ascii=False), flush=True)


def plain(v):
    if isinstance(v, Variant):
        return plain(v.value)
    if isinstance(v, (bytes, bytearray)):
        return f"<{len(v)} bytes>"
    if isinstance(v, list):
        return [plain(x) for x in v]
    if isinstance(v, dict):
        return {k: plain(x) for k, x in v.items()}
    return v


class Watcher(ServiceInterface):
    def __init__(self):
        super().__init__("org.kde.StatusNotifierWatcher")
        self.items = []

    @method()
    def RegisterStatusNotifierItem(self, service: "s"):
        sender = self._Watcher__sender
        name, path = (sender, service) if service.startswith("/") else (service, "/StatusNotifierItem")
        global item
        item = (name, path)
        self.items = [f"{name}{path}"]
        out(event="register", service=service, sender=sender)
        asyncio.get_event_loop().create_task(dump_item())

    @method()
    def RegisterStatusNotifierHost(self, service: "s"):
        pass

    @dbus_property(access=PropertyAccess.READ)
    def RegisteredStatusNotifierItems(self) -> "as":
        return self.items

    @dbus_property(access=PropertyAccess.READ)
    def IsStatusNotifierHostRegistered(self) -> "b":
        return True

    @dbus_property(access=PropertyAccess.READ)
    def ProtocolVersion(self) -> "i":
        return 0


class Notifications(ServiceInterface):
    def __init__(self):
        super().__init__("org.freedesktop.Notifications")
        self.next_id = 1

    @method()
    def Notify(self, app_name: "s", replaces_id: "u", app_icon: "s", summary: "s", body: "s",
               actions: "as", hints: "a{sv}", expire_timeout: "i") -> "u":
        nid = replaces_id or self.next_id
        if not replaces_id:
            self.next_id += 1
        out(event="notify", id=nid, replaces=replaces_id, app=app_name, icon=app_icon, summary=summary,
            body=body, hints=plain(hints), timeout=expire_timeout)
        return nid

    @method()
    def GetCapabilities(self) -> "as":
        return ["body", "body-markup"]

    @method()
    def GetServerInformation(self) -> "ssss":
        return ["mock", "test", "1", "1.2"]

    @method()
    def CloseNotification(self, nid: "u"):
        pass


async def call(path, iface, member, signature="", body=None, dest=None):
    reply = await bus.call(Message(destination=dest or item[0], path=path, interface=iface, member=member,
                                   signature=signature, body=body or []))
    if reply.message_type == MessageType.ERROR:
        out(event="error", member=member, error=reply.error_name, body=reply.body)
        return None
    return reply.body


def layout_tree(node):
    nid, props, children = node
    d = {"id": nid}
    p = plain(props)
    if p.get("type") == "separator":
        d["sep"] = True
    else:
        d["label"] = p.get("label")
        if p.get("enabled") is False:
            d["disabled"] = True
    if children:
        d["children"] = [layout_tree(plain(c)) for c in children]
    return d


async def dump_menu():
    r = await call("/MenuBar", "com.canonical.dbusmenu", "GetLayout", "iias", [0, -1, []])
    if r:
        out(event="menu", revision=r[0], layout=layout_tree(r[1]))


async def dump_item():
    global owner
    if not item:
        return
    r = await call("/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner", "s", [item[0]],
                   dest="org.freedesktop.DBus")
    owner = r[0] if r else item[0]
    r = await call(item[1], "org.freedesktop.DBus.Properties", "GetAll", "s", ["org.kde.StatusNotifierItem"])
    if r:
        props = r[0]
        pix = [(w, h, len(data)) for w, h, data in props["IconPixmap"].value]
        p = plain(props)
        p["IconPixmap"] = pix
        out(event="item", props=p)
    await dump_menu()


def on_message(msg):
    if msg.message_type != MessageType.SIGNAL or not item or msg.sender not in (item[0], owner):
        return
    if msg.member in ("NewToolTip", "NewTitle", "NewIcon", "NewStatus", "LayoutUpdated"):
        out(event="signal", member=msg.member, body=plain(msg.body))
        if msg.member == "NewToolTip":
            async def tip():
                r = await call(item[1], "org.freedesktop.DBus.Properties", "Get", "ss",
                               ["org.kde.StatusNotifierItem", "ToolTip"])
                if r:
                    t = plain(r[0])
                    out(event="tooltip", title=t[2], text=t[3])
            asyncio.get_event_loop().create_task(tip())


async def control():
    loop = asyncio.get_event_loop()
    while True:
        line = await loop.run_in_executor(None, sys.stdin.readline)
        if not line:
            await asyncio.sleep(0.2)
            continue
        cmd = line.split()
        if not cmd:
            continue
        if cmd[0] == "quit-watcher":
            await bus.release_name("org.kde.StatusNotifierWatcher")
            out(event="watcher-gone")
            continue
        if cmd[0] == "restart-watcher":
            await bus.request_name("org.kde.StatusNotifierWatcher")
            out(event="watcher-back")
            continue
        if not item:
            out(event="error", error="no item registered")
            continue
        if cmd[0] == "activate":
            await call(item[1], "org.kde.StatusNotifierItem", "Activate", "ii", [0, 0])
        elif cmd[0] == "secondary":
            await call(item[1], "org.kde.StatusNotifierItem", "SecondaryActivate", "ii", [0, 0])
        elif cmd[0] == "scroll":
            await call(item[1], "org.kde.StatusNotifierItem", "Scroll", "is", [int(cmd[1]), "vertical"])
        elif cmd[0] == "click":
            await call("/MenuBar", "com.canonical.dbusmenu", "AboutToShow", "i", [0])
            await call("/MenuBar", "com.canonical.dbusmenu", "Event", "isvu",
                       [int(cmd[1]), "clicked", Variant("i", 0), 0])
        elif cmd[0] == "menu":
            await dump_menu()
        elif cmd[0] == "item":
            await dump_item()
        out(event="done", command=line.strip())


async def main():
    global bus
    bus = await MessageBus().connect()
    watcher = Watcher()
    orig = bus._make_method_handler

    def handler(interface, m):
        h = orig(interface, m)

        def wrapped(msg, send_reply):
            interface._Watcher__sender = msg.sender
            return h(msg, send_reply)
        return wrapped
    bus._make_method_handler = handler
    bus.export("/StatusNotifierWatcher", watcher)
    bus.export("/org/freedesktop/Notifications", Notifications())
    await bus.request_name("org.freedesktop.Notifications")
    if not args.no_watcher:
        await bus.request_name("org.kde.StatusNotifierWatcher")
    # all signals of the item (tool tip, menu revisions)
    await call("/org/freedesktop/DBus", "org.freedesktop.DBus", "AddMatch", "s", ["type='signal'"],
               dest="org.freedesktop.DBus")
    bus.add_message_handler(on_message)
    out(event="ready")
    await control()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-watcher", action="store_true")
    args = ap.parse_args()
    asyncio.run(main())
