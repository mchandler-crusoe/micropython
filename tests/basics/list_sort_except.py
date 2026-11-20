# if a comparison or key function raises part way through a sort, the list
# must still hold every one of its original items (in some order)


class Boom(Exception):
    pass


class Item:
    budget = 0

    def __init__(self, x):
        self.x = x

    def __lt__(self, other):
        Item.budget -= 1
        if Item.budget < 0:
            raise Boom
        return self.x < other.x


def keys(n, seed):
    out = []
    x = seed
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out.append((x >> 8) % 50)
    return out


try:
    probe = [[Item(0) for _ in range(100)] for _ in range(4)]
except MemoryError:
    print("SKIP")
    raise SystemExit
probe = None

ok = True
for n in (2, 3, 10, 40, 100):
    xs = keys(n, n)
    for budget in (0, 1, 2, 5, 17, 60, 300):
        l = [Item(x) for x in xs]
        ids = sorted([id(it) for it in l])
        Item.budget = budget
        try:
            l.sort()
        except Boom:
            pass
        if len(l) != n or sorted([id(it) for it in l]) != ids:
            ok = False
            print("LOST", n, budget)
        l = None
        ids = None
        # key function that raises
        calls = [budget]

        def key(it):
            calls[0] -= 1
            if calls[0] < 0:
                raise Boom
            return it.x

        l = [Item(x) for x in xs]
        ids = sorted([id(it) for it in l])
        Item.budget = 1 << 30
        try:
            l.sort(key=key)
        except Boom:
            pass
        if len(l) != n or sorted([id(it) for it in l]) != ids:
            ok = False
            print("LOST key", n, budget)
print(ok)
