# test that list.sort() and sorted() are stable: items that compare equal
# keep their original relative order, with and without key and reverse


class Item:
    def __init__(self, k, i):
        self.k = k
        self.i = i

    def __lt__(self, other):
        return self.k < other.k


def check(l, reverse=False):
    for j in range(len(l) - 1):
        a = l[j]
        b = l[j + 1]
        if a.k == b.k:
            if a.i > b.i:
                return False
        elif (b.k < a.k) != reverse:
            return False
    return True


# pseudo random keys with many duplicates, deterministic across ports and
# small enough for builds without long ints
def keys(n, m, seed):
    out = []
    x = seed
    for _ in range(n):
        x = (x * 75 + 74) % 65537
        out.append(x % m)
    return out


# skip on targets without room for the largest lists: sorted() holds two
# lists at once; probe for more than that to allow for fragmentation
try:
    probe = [[Item(0, i) for i in range(300)] for _ in range(3)]
except MemoryError:
    print("SKIP")
    raise SystemExit
probe = None

# Sort in a function so that no stale reference to a list is left in this
# frame's value stack, which the conservative GC would keep alive.
def run(ks, reverse):
    l = [Item(ks[i], i) for i in range(len(ks))]
    l.sort(reverse=reverse)
    a = check(l, reverse)
    l = None
    l = [Item(ks[i], i) for i in range(len(ks))]
    l = sorted(l, key=lambda it: it.k, reverse=reverse)
    return a and check(l, reverse)


ok = True
for n in (0, 1, 2, 3, 5, 8, 13, 31, 32, 33, 64, 100, 257, 300):
    for m in (1, 2, 3, 10, 1000):
        ks = keys(n, m, n + m)
        for reverse in (False, True):
            if not run(ks, reverse):
                ok = False
                print("FAIL", n, m, reverse)
print(ok)

# runs that are already in order, in reverse, or interleaved stay stable
def run2(f, n):
    l = [Item(f(i), i) for i in range(n)]
    l.sort()
    a = check(l)
    l = None
    l = [Item(f(i), i) for i in range(n)]
    l.sort(reverse=True)
    return a, check(l, True)


n = 200
for f in (
    lambda j: j // 4,
    lambda j: (n - j) // 4,
    lambda j: j % 7,
    lambda j: 0,
):
    a, b = run2(f, n)
    print(a, b)

# stable sort on a key that groups ints by parity
l = list(range(20))
l.sort(key=lambda x: x % 2)
print(l)
l = list(range(20))
l.sort(key=lambda x: x % 3, reverse=True)
print(l)
