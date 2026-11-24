#!/usr/bin/env python3
"""The kinds' own state: lists it, or moves it into each class's nested `State` struct.

A kind (a class derived from Entity, directly or not) keeps what it remembers in one plain struct, `Saved`, as its
member `s`: that is what lets an entity be written out and read back without naming its fields (lvr/archive.hpp).

  kind_state.py list FILE...     the data members each kind declares outside its Saved
  kind_state.py check FILE...    the same; exit status 1 if there are any (scripts/check.sh)
  kind_state.py move FILE...     moves them into Saved and renames their uses: `timer_` becomes `s.timer`
"""
import re
import sys

CLASS = re.compile(r'^(class|struct) (\w+)( final)? : (public|private) ([\w:]+)[^{;]*\{', re.M)
SKIP = ('using ', 'static ', 'friend ', 'enum ', 'struct ', 'class ', 'template', 'typedef ', 'public:', 'private:',
        'protected:', 'LVR_', 'explicit ', 'virtual ', 'constexpr ', 'inline ', 'return ', '~')


def body_end(text, open_brace):
    depth = 0
    i = open_brace
    while i < len(text):
        c = text[i]
        if c == '/' and text[i + 1] == '/':
            i = text.index('\n', i)
            continue
        if c == '"':
            i += 1
            while text[i] != '"':
                i += 2 if text[i] == '\\' else 1
        elif c == "'":
            i += 1
            while text[i] != "'":
                i += 2 if text[i] == '\\' else 1
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError('unbalanced')


def statements(text, start, end):
    """The statements at the class body's own depth: (start, end, text), comments before them left out."""
    out = []
    depth = 0
    i = start
    begin = start
    while i < end:
        c = text[i]
        if c == '/' and text[i + 1] == '/':
            j = text.index('\n', i)
            if depth == 0 and text[begin:i].strip() == '':
                begin = j + 1
            i = j
            continue
        if c == '"' or c == "'":
            q = c
            i += 1
            while text[i] != q:
                i += 2 if text[i] == '\\' else 1
        elif c in '{(':
            depth += 1
        elif c in '})':
            depth -= 1
            if depth == 0 and c == '}':
                # a function body or a nested type ends here if no ';' is needed
                j = i + 1
                while j < end and text[j] in ' \t':
                    j += 1
                head = text[begin:i + 1].strip()
                if text[j] != ';' and not re.match(r'^[^=]*\b\w+_?\s*(\{|=)', head.split('\n')[0]) or '(' in head.split('{')[0]:
                    if text[j] != ';':
                        begin = i + 1
        elif c == ';' and depth == 0:
            out.append((begin, i + 1, text[begin:i + 1]))
            begin = i + 1
        elif c == ':' and depth == 0 and text[begin:i].strip() in ('public', 'private', 'protected'):
            begin = i + 1
        i += 1
    return out


def is_member(statement):
    s = statement.strip()
    if not s or s.startswith(SKIP):
        return None
    head = re.split(r'[={]', s, maxsplit=1)[0]
    if '(' in head or 'operator' in head:
        return None
    m = re.search(r'(\w+)\s*(\[[^\]]*\])?\s*$', head.rstrip(';').strip())
    if not m:
        return None
    return m.group(1)


def kinds_of(text, known):
    found = []
    for m in CLASS.finditer(text):
        name, base = m.group(2), m.group(5)
        if base.split('::')[-1] not in known and base not in known:
            continue
        known.add(name)
        open_brace = m.end() - 1
        close = body_end(text, open_brace)
        found.append((name, base, open_brace, close))
    return found


def members_of(text, open_brace, close):
    out = []
    for begin, end, statement in statements(text, open_brace + 1, close):
        name = is_member(statement)
        if name is not None:
            out.append((begin, end, statement, name))
    return out


# A kind others are derived from names its member otherwise, so that a derived kind's own `s` doesn't hide it.
BASE_MEMBER = {'Enemy': 'enemy', 'StraightShot': 'shot', 'WallThatGoes': 'wall'}


def renamed(text, names, member):
    for name in names:
        text = re.sub(r'\b' + re.escape(name) + r'\b', f'{member}.{name.rstrip("_")}', text)
    return text


def move(files):
    known = {'Entity'}
    texts = {f: open(f).read() for f in files}
    for _ in range(2):
        for f in files:
            kinds_of(texts[f], known)
    # What each kind declares, and what it is derived from.
    declared = {}
    for f in files:
        for name, base, open_brace, close in kinds_of(texts[f], known):
            declared[name] = (base.split('::')[-1], [m[3] for m in members_of(texts[f], open_brace, close)])

    def inherited(name):
        out = []
        base = declared[name][0]
        while base in declared:
            if declared[base][1]:
                out.append((BASE_MEMBER[base], declared[base][1]))
            base = declared[base][0]
        return out

    for f in files:
        text = texts[f]
        for name, base, open_brace, close in reversed(kinds_of(text, known)):
            own = declared[name][1]
            member = BASE_MEMBER.get(name, 's')
            body = text[open_brace + 1:close]
            lines = body.split('\n')
            offsets = []
            at = open_brace + 1
            for line in lines:
                offsets.append(at)
                at += len(line) + 1

            def line_of(position):
                for i in range(len(offsets) - 1, -1, -1):
                    if offsets[i] <= position:
                        return i
                return 0

            taken = set()
            spans = []
            for begin, end, statement, declared_name in members_of(text, open_brace, close):
                first = line_of(begin + len(statement) - len(statement.lstrip()))
                last = line_of(end - 1)
                # Its comment: the comment lines right above it, and what a comment at its end goes on with below.
                while first > 0 and lines[first - 1].strip().startswith('//') and (first - 1) not in taken:
                    first -= 1
                column = lines[last].find('//', lines[last].find(';'))
                while column >= 0 and last + 1 < len(lines) and lines[last + 1].strip().startswith('//') and \
                        lines[last + 1].find('//') == column:
                    last += 1
                spans.append((first, last, declared_name))
                taken.update(range(first, last + 1))
            # Uses, everywhere but in the declarations themselves.
            names_of_bases = [(m, [n for n in names if n not in own]) for m, names in inherited(name)]
            for i, line in enumerate(lines):
                if i in taken:
                    continue
                for base_member, names in names_of_bases:
                    line = renamed(line, names, base_member)
                lines[i] = renamed(line, own, member)
            if spans:
                indent = re.match(r' *', lines[spans[0][0]]).group(0)
                struct = [f'{indent}struct Saved {{']
                for first, last, declared_name in spans:
                    for i in range(first, last + 1):
                        line = re.sub(r'\b' + re.escape(declared_name) + r'\b', declared_name.rstrip('_'), lines[i])
                        struct.append('    ' + line if line.strip() else line)
                struct.append(f'{indent}}} {member};')
                struct.append(f'{indent}LVR_SAVES({base})' if member == 's' else
                              f'{indent}LVR_SAVES_AS({base}, {member})')
                insert_at = spans[0][0]
                lines = [line for i, line in enumerate(lines) if i not in taken or i == insert_at]
                # (the first taken line is still there, as the place)
                place = sum(1 for i in range(insert_at) if i not in taken)
                lines[place:place + 1] = struct
            text = text[:open_brace + 1] + '\n'.join(lines) + text[close:]
        texts[f] = text
    # Functions defined outside their class (the Vikings'): the names of that class in the files that have them.
    for name, (base, own) in declared.items():
        if not own:
            continue
        member = BASE_MEMBER.get(name, 's')
        for f in files:
            if re.search(r'\b' + name + r'::\w+\(', texts[f]):
                texts[f] = renamed(texts[f], own, member)
    for f in files:
        if texts[f] != open(f).read():
            open(f, 'w').write(texts[f])
            print('moved:', f)


def main():
    mode, files = sys.argv[1], sys.argv[2:]
    if mode == 'move':
        move(files)
        return
    known = {'Entity'}
    texts = {f: open(f).read() for f in files}
    # Bases first: headers before sources, and twice so a base later in the list is known.
    for _ in range(2):
        for f in files:
            kinds_of(texts[f], known)
    stray = 0
    for f in files:
        text = texts[f]
        for name, base, open_brace, close in kinds_of(text, known):
            for begin, end, statement, member in members_of(text, open_brace, close):
                if 'Saved' in statement:  # its state: the one member a kind has
                    continue
                stray += 1
                line = text.count('\n', 0, begin + len(statement) - len(statement.lstrip())) + 1
                print(f'{f}:{line}: {name} : {base}: {" ".join(statement.split())[:110]}')
    if mode == 'check' and stray:
        print(f'{stray} data members of kinds outside their Saved', file=sys.stderr)
        sys.exit(1)
    if mode == 'list':
        print(f'{len(known) - 1} kinds, {stray} data members outside Saved')


main()
