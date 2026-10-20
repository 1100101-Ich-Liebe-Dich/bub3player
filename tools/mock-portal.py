#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A stand-in for the file chooser of the XDG desktop portal, for testing the
desktop's own dialogs without a desktop (needs dbus-next: `pip install dbus-next`
or the package python3-dbus-next):

    dbus-run-session -- sh -c 'tools/mock-portal.py --answer /tmp/musik & build/halcyon'

Every OpenFile/SaveFile call is printed (parent window, title, options) and
answered after a moment with the given path, or as cancelled (--cancel).
--version sets the reported interface version (folders need 3), --fail answers
at once with 2 (a backend that cannot show a dialog), --filter N reports the
N-th filter as the chosen one when saving.
"""
import argparse, asyncio, json, urllib.parse
from dbus_next.aio import MessageBus
from dbus_next.service import ServiceInterface, method, signal, dbus_property, PropertyAccess
from dbus_next import Variant

args = None
bus = None


class Request(ServiceInterface):
    def __init__(self):
        super().__init__("org.freedesktop.portal.Request")

    @signal()
    def Response(self, response: "u", results: "a{sv}") -> "ua{sv}":
        return [response, results]

    @method()
    def Close(self):
        pass


def plain(v):
    if isinstance(v, Variant):
        return plain(v.value)
    if isinstance(v, (bytes, bytearray)):
        return v.rstrip(b"\0").decode(errors="replace")
    if isinstance(v, list):
        return [plain(x) for x in v]
    if isinstance(v, dict):
        return {k: plain(x) for k, x in v.items()}
    return v


class FileChooser(ServiceInterface):
    def __init__(self):
        super().__init__("org.freedesktop.portal.FileChooser")

    @dbus_property(access=PropertyAccess.READ)
    def version(self) -> "u":
        return args.version

    def answer(self, kind, parent, title, options):
        sender = self.__sender.lstrip(":").replace(".", "_")
        token = plain(options.get("handle_token", "t"))
        path = f"/org/freedesktop/portal/desktop/request/{sender}/{token}"
        print(json.dumps({"call": kind, "parent": parent, "title": title, "options": plain(options)}), flush=True)
        req = Request()
        bus.export(path, req)

        async def later():
            if args.fail:
                await asyncio.sleep(0.05)
                req.Response(2, {})
                return
            await asyncio.sleep(0.4)
            if args.cancel:
                req.Response(1, {})
            else:
                res = {"uris": Variant("as", ["file://" + urllib.parse.quote(args.answer)])}
                filters = plain(options.get("filters", []))
                if kind == "SaveFile" and 0 <= args.filter < len(filters):
                    name, rules = filters[args.filter]
                    res["current_filter"] = Variant("(sa(us))", [name, [list(r) for r in rules]])
                req.Response(0, res)
            await asyncio.sleep(0.2)
            bus.unexport(path)

        asyncio.get_event_loop().create_task(later())
        return path

    @method()
    def OpenFile(self, parent_window: "s", title: "s", options: "a{sv}") -> "o":
        return self.answer("OpenFile", parent_window, title, options)

    @method()
    def SaveFile(self, parent_window: "s", title: "s", options: "a{sv}") -> "o":
        return self.answer("SaveFile", parent_window, title, options)


async def main():
    global bus
    bus = await MessageBus().connect()
    chooser = FileChooser()
    # dbus-next hands the message to the method only through this hook
    orig = bus._make_method_handler

    def handler(interface, m):
        h = orig(interface, m)

        def wrapped(msg, send_reply):
            interface._FileChooser__sender = msg.sender
            return h(msg, send_reply)
        return wrapped
    bus._make_method_handler = handler
    bus.export("/org/freedesktop/portal/desktop", chooser)
    await bus.request_name("org.freedesktop.portal.Desktop")
    print("mock portal ready", flush=True)
    await asyncio.Future()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--answer", default="/tmp")
    ap.add_argument("--cancel", action="store_true")
    ap.add_argument("--version", type=int, default=4)
    ap.add_argument("--fail", action="store_true")
    ap.add_argument("--filter", type=int, default=-1)
    args = ap.parse_args()
    asyncio.run(main())
