import os
import shutil


def remove_content(location, names=[], extensions=[]):
    assert names or extensions
    for item in os.listdir(location):
        if item in names or any([item.endswith(ext) for ext in extensions]):
            path = os.path.join(location, item)
            if os.path.isdir(path):
                shutil.rmtree(path)
            else:
                os.remove(path)


def distribute_patterns(names, patterns):
    if not patterns or [] in patterns:
        return [(n, []) for n in names]

    for p in patterns:
        if not any([p[0] in n for n in names]):
            raise Exception("No match for pattern '{}'".format(".".join(p)))

    matches = []
    for n in names:
        ps = [p[1:] for p in patterns if p[0] in n]

        if len(ps) > 0:
            matches.append((n, ps))

    return matches

