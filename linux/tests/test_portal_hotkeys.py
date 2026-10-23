#!/usr/bin/python3
"""Private-session portal protocol test. Never contacts the desktop portal."""
import os
import subprocess
import sys
import time
from gi.repository import Gio, GLib

assert os.environ.get('FXSOUND_PRIVATE_PORTAL_TEST') == '1'
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
              'RequestName', GLib.Variant('(su)', ('org.freedesktop.portal.Desktop', 0)), None, 0, 2000, None)
xml = '''<node><interface name="org.freedesktop.portal.GlobalShortcuts">
<method name="CreateSession"><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
<method name="BindShortcuts"><arg type="o" direction="in"/><arg type="a(sa{sv})" direction="in"/><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
</interface></node>'''
registry_xml = '<node><interface name="org.freedesktop.host.portal.Registry"><method name="Register"><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/></method></interface></node>'
registered = set()
def register(connection, sender, path, iface, method, args, invocation):
    assert args.unpack()[0] == 'fxsound'
    registered.add(sender)
    invocation.return_value(None)
bus.register_object('/org/freedesktop/portal/desktop', Gio.DBusNodeInfo.new_for_xml(registry_xml).interfaces[0], register, None, None)
close_xml = '<node><interface name="org.freedesktop.portal.Session"><method name="Close"/></interface></node>'
request_xml = '<node><interface name="org.freedesktop.portal.Request"><method name="Close"/></interface></node>'
created, bound, closed, cancelled = [], [], [], []
mode = 'allow'
counter = 0

def close_call(connection, sender, path, iface, method, args, invocation):
    (closed if iface.endswith('Session') else cancelled).append(path)
    invocation.return_value(None)

def call(connection, sender, path, iface, method, args, invocation):
    global counter
    assert sender in registered, 'portal connection has no registered application identity'
    counter += 1
    request = '/org/freedesktop/portal/desktop/request/test/r' + str(counter)
    bus.register_object(request, Gio.DBusNodeInfo.new_for_xml(request_xml).interfaces[0], close_call, None, None)
    invocation.return_value(GLib.Variant('(o)', (request,)))
    if method == 'CreateSession':
        session = '/org/freedesktop/portal/desktop/session/test/s' + str(counter)
        created.append(session)
        bus.register_object(session, Gio.DBusNodeInfo.new_for_xml(close_xml).interfaces[0], close_call, None, None)
        result = {'session_handle': GLib.Variant('s', session)}
        code = 0
    else:
        bound.append(args.unpack())
        result, code = {}, (1 if mode == 'deny' else 0)
        if mode == 'hang':
            return
    def respond():
        bus.emit_signal(sender, request, 'org.freedesktop.portal.Request', 'Response',
                        GLib.Variant('(ua{sv})', (code, result)))
        return False
    GLib.timeout_add(10, respond)

bus.register_object('/org/freedesktop/portal/desktop', Gio.DBusNodeInfo.new_for_xml(xml).interfaces[0], call, None, None)
child = subprocess.Popen([sys.argv[1]], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
os.set_blocking(child.stdout.fileno(), False)
output = ''
def pump(predicate, timeout=5):
    global output
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        while GLib.MainContext.default().iteration(False):
            pass
        try:
            output += os.read(child.stdout.fileno(), 65536).decode()
        except BlockingIOError:
            pass
        if predicate():
            return
        time.sleep(.01)
    raise AssertionError(('timeout', output, created, bound, closed, cancelled))
def send(command):
    child.stdin.write(command + '\n'); child.stdin.flush()
def activate(session, command):
    bus.emit_signal(None, '/org/freedesktop/portal/desktop', 'org.freedesktop.portal.GlobalShortcuts',
                    'Activated', GLib.Variant('(osta{sv})', (session, str(command), 1, {})))
try:
    send('set 1001 3 65\nset 1002 6 112')
    pump(lambda: len(bound) == 1)
    assert bound[0][1][0][1]['preferred_trigger'] == 'CTRL+ALT+A'
    assert bound[0][1][1][1]['preferred_trigger'] == 'CTRL+SHIFT+F1'
    time.sleep(.1)
    activate(created[-1], 1001)
    pump(lambda: 'activated 1001' in output)
    send('set 1001 3 66')
    pump(lambda: len(bound) == 2 and len(closed) >= 1)
    assert bound[1][1][0][1]['preferred_trigger'] == 'CTRL+ALT+B'
    ready = time.monotonic() + .2
    pump(lambda: time.monotonic() >= ready)
    send('clear')
    pump(lambda: len(closed) == 2)
    before = output.count('activated')
    activate(created[0], 1001)
    send('set 1003 0 1')  # unsupported key is rejected
    pump(lambda: 'queued 0' in output)
    assert output.count('activated') == before
    mode = 'deny'
    send('set 1001 3 67')
    pump(lambda: len(closed) == 3)
    assert len(bound) == 3  # denial must not cause a permission-prompt loop
    mode = 'hang'
    send('set 1001 3 68')
    pump(lambda: len(bound) == 4)
    send('clear')
    pump(lambda: len(cancelled) == 1 and len(closed) == 4)
    send('set 1001 3 69')
    pump(lambda: len(bound) == 5)
    send('quit')
    pump(lambda: child.poll() is not None)
    assert child.returncode == 0, child.stderr.read()
    print('PASS: triggers, activation, rebind, clear, invalid key, denial, cancellation, pending shutdown')
finally:
    if child.poll() is None:
        child.kill(); child.wait()
