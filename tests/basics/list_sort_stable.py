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


# pseudo random keys with many duplicates, deterministic across ports
def keys(n, m, seed):
    out = []
    x = seed
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out.append((x >> 8) % m)
    return out


# skip on targets without room for the largest lists (with margin for
# fragmentation)
try:
    probe = [[Item(0, i) for i in range(300)] for _ in range(3)]
except MemoryError:
    print("SKIP")
    raise SystemExit
probe = None

ok = True
for n in (0, 1, 2, 3, 5, 8, 13, 31, 32, 33, 64, 100, 257, 300):
    for m in (1, 2, 3, 10, 1000):
        ks = keys(n, m, n + m)
        for reverse in (False, True):
            l = [Item(ks[i], i) for i in range(len(ks))]
            l.sort(reverse=reverse)
            if not check(l, reverse):
                ok = False
                print("FAIL", n, m, reverse)
            # same via key= on plain ints-carrying tuples' index
            l = None
            l = [Item(ks[i], i) for i in range(len(ks))]
            l = sorted(l, key=lambda it: it.k, reverse=reverse)
            if not check(l, reverse):
                ok = False
                print("FAIL key", n, m, reverse)
            l = None
print(ok)

# runs that are already in order, in reverse, or interleaved stay stable
n = 200
for ks in (
    [j // 4 for j in range(n)],
    [(n - j) // 4 for j in range(n)],
    [j % 7 for j in range(n)],
    [0] * n,
):
    l = [Item(ks[i], i) for i in range(len(ks))]
    l.sort()
    a = check(l)
    l = None
    l = [Item(ks[i], i) for i in range(len(ks))]
    l.sort(reverse=True)
    print(a, check(l, True))
    l = None
ks = None

# stable sort on a key that groups ints by parity
l = list(range(20))
l.sort(key=lambda x: x % 2)
print(l)
l = list(range(20))
l.sort(key=lambda x: x % 3, reverse=True)
print(l)
