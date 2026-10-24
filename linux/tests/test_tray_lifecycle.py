#!/usr/bin/env python3
"""Exercise the real tray with a private desktop-host fixture."""
from gi.repository import Gio, GLib
import select
import subprocess
import sys
import time

probe = sys.argv[1]
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
registrations = []
children = []
held = []
withhold = False
node = Gio.DBusNodeInfo.new_for_xml('''<node><interface name="org.kde.StatusNotifierWatcher">
<method name="RegisterStatusNotifierItem"><arg type="s" direction="in"/></method>
</interface></node>''')

def pump(test, seconds=4):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        while GLib.MainContext.default().iteration(False):
            pass
        if test(): return True
        time.sleep(.005)
    return False

def line(child):
    assert select.select([child.stdout], [], [], 5)[0], 'tray probe did not respond'
    value = child.stdout.readline().strip()
    assert value, 'tray probe exited'
    return value

def command(child, value):
    child.stdin.write(value + '\n')
    child.stdin.flush()

def prop(child, key):
    return bus.call_sync(f'org.kde.StatusNotifierItem-{child.pid}-1', '/StatusNotifierItem',
        'org.freedesktop.DBus.Properties', 'Get',
        GLib.Variant('(ss)', ('org.kde.StatusNotifierItem', key)),
        GLib.VariantType.new('(v)'), Gio.DBusCallFlags.NO_AUTO_START, 1000, None).unpack()[0]

def start():
    child = subprocess.Popen([probe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(child)
    assert line(child) == 'READY'
    def ready():
        try: return prop(child, 'Id') == 'fxsound'
        except GLib.Error: return False
    assert pump(ready), 'tray did not export its interface'
    return child

def stop(child):
    command(child, 'quit')
    assert child.wait(timeout=2) == 0, 'tray shutdown blocked'

def handler(conn, sender, path, interface, method, args, invocation):
    registrations.append(args.unpack()[0])
    if withhold: held.append(invocation)
    else: invocation.return_value(None)

owner = None
registration = None
try:
    for _ in range(100):
        subprocess.run([probe, 'immediate'], check=True, timeout=2)
    print('PASS: 100 immediate tray shutdowns', flush=True)
    child = start()
    registration = bus.register_object('/StatusNotifierWatcher', node.interfaces[0], handler, None, None)
    owner = Gio.bus_own_name_on_connection(bus, 'org.kde.StatusNotifierWatcher', Gio.BusNameOwnerFlags.NONE, None, None)
    assert pump(lambda: len(registrations) == 1), 'late desktop tray host was never registered with'
    assert pump(lambda: (command(child, 'active'), line(child))[1] == 'ACTIVE 1')
    print('PASS: late desktop tray host registration', flush=True)

    Gio.bus_unown_name(owner)
    owner = None
    assert pump(lambda: (command(child, 'active'), line(child))[1] == 'ACTIVE 0')
    owner = Gio.bus_own_name_on_connection(bus, 'org.kde.StatusNotifierWatcher', Gio.BusNameOwnerFlags.NONE, None, None)
    assert pump(lambda: len(registrations) == 2), 'desktop tray host restart was not registered with'
    print('PASS: desktop tray host restart', flush=True)

    command(child, 'stress')
    for _ in range(100):
        title = prop(child, 'Title')
        assert isinstance(title, str)
        pixels = prop(child, 'IconPixmap')
        if pixels:
            width, height, values = pixels[0]
            assert width == height == 8 and len(values) == width * height * 4, 'torn icon snapshot'
    assert line(child) == 'UPDATED'
    stop(child)
    print('PASS: concurrent property/menu updates and desktop queries', flush=True)

    withhold = True
    pending = start()
    assert pump(lambda: bool(held)), 'fixture did not receive registration'
    stop(pending)
    print('PASS: shutdown cancels an unanswered desktop registration', flush=True)
finally:
    if owner is not None: Gio.bus_unown_name(owner)
    if registration is not None: bus.unregister_object(registration)
    for invocation in held:
        invocation.return_dbus_error('org.freedesktop.DBus.Error.Cancelled', 'fixture stopped')
    for child in children:
        if child.poll() is None:
            child.kill()
            child.wait(timeout=5)
