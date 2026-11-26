"""Lecture / écriture minimale des S-expressions KiCad."""
import re

class Q(str):
    """Chaîne entre guillemets (conserve la distinction avec les atomes)."""

_TOK = re.compile(r'\(|\)|"(?:\\.|[^"\\])*"|[^\s()]+')

def parse(text):
    stack, cur = [], []
    for t in _TOK.findall(text):
        if t == "(":
            stack.append(cur); cur = []
        elif t == ")":
            done = cur; cur = stack.pop(); cur.append(done)
        elif t.startswith('"'):
            cur.append(Q(t[1:-1].replace('\\"', '"').replace("\\\\", "\\")))
        else:
            cur.append(t)
    return cur

def dump(e):
    if isinstance(e, list):
        return "(" + " ".join(dump(x) for x in e) + ")"
    if isinstance(e, Q):
        return '"' + e.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return str(e)

def find(e, key):
    """Premier sous-élément (key ...)."""
    for x in e:
        if isinstance(x, list) and x and x[0] == key:
            return x
    return None

def findall(e, key):
    return [x for x in e if isinstance(x, list) and x and x[0] == key]
