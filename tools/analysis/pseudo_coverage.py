import os
funcs = os.listdir('build/orig_pseudo/functions')
covered, missing = 0, 0
miss = {}
for line in open('docs/assets/rgss_ruby_api_map.tsv', encoding='utf-8').read().splitlines()[1:]:
    p = line.split('\t')
    if len(p) >= 5 and p[4]:
        va = p[4].lower()          # e.g. 0x10003460
        name = p[0] + '.' + (p[2] or p[1])
        if va[2:] in ' '.join(funcs):  # match 10003460 inside filename
            covered += 1
        else:
            missing += 1
            miss.setdefault(p[0], []).append(name + '@' + va)
print('api targets covered: %d / missing: %d' % (covered, missing))
for cls in sorted(miss):
    print('  %s: %d missing' % (cls, len(miss[cls])))
