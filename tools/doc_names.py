"""Names the docs and the RAM map cite that the code no longer has.

    python3 tools/doc_names.py            # every stale name, where it is cited
    python3 tools/doc_names.py --check    # the same, failing on any; the suite runs it

A branch that renames or deletes a function passes its own review and then
leaves docs/TODO.md, a skill or a `source` string in re/ram.toml pointing at
something that isn't there, and the next reader goes looking for it. This check
collects the code names those texts cite and reports each one the code doesn't
have:

  - identifier-shaped names in backticks in docs/*.md (not docs/history/,
    which is a record of what was), CLAUDE.md and .claude/skills/*/SKILL.md:
    snake_case (an underscore between letters) or CamelCase (an internal
    capital), so an ordinary word in backticks is not one. A name ending in
    `_` or `*` stands for every name it begins (`field_upload_layer*`);
  - the same names cited in re/ram.toml's `source`, `meaning`, `why` and
    `unchecked` strings, which are prose about the code;
  - paths in backticks (`field/scratch.h`, `screen_script.c`) and in the
    RAM map's prose and `#` comments, checked as files: a path matches any
    file in the tree that it ends.

A name exists if it is an identifier of the code in game/, runtime/,
frontend/, tools/, tests/, re/ or CMakeLists.txt, or the name of a file
there. Comments, docstrings, strings (but a string that is one identifier, an
environment variable's name), the ledgers in tools/ and the RAM map's prose
mention names without defining them, so they don't keep a name alive. A mention of a name that is gone on purpose (a bad
pattern given as an example, a format the reader may still meet) goes in
tools/doc_names_exceptions.txt with the reason on its line; rewording the doc
so it names what is there now is usually better.
"""
import argparse
import os
import pathlib
import re
import sys
import time
import tokenize

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXCEPTIONS = 'tools/doc_names_exceptions.txt'
CODE_DIRS = ('game', 'runtime', 'frontend', 'tools', 'tests', 're')
CODE_FILES = ('CMakeLists.txt',)
# Ledgers (tools/*.txt, *.tsv) only mention names, so they don't define one;
# the RAM map's tables in re/ are where the accessors' names come from.
CODE_SUFFIXES = {'.c', '.h', '.inc', '.py', '.toml', '.cmake', '.in', '.json'}
RE_TABLES = {'.tsv'}
# The check's own files cite stale names as examples; they don't keep one alive.
SELF = {'tools/doc_names.py', EXCEPTIONS, 'tests/test_doc_names.py'}
PROSE_KEYS = ('source', 'meaning', 'why', 'unchecked')

WORD = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
SPAN = re.compile(r'`([^`\n]+)`')
# A name with a trailing `_` or `*` is a prefix; one with a leading `_` or `*`
# a suffix.
NAME = re.compile(r'(?<![\w$.:/-])(\*?)([A-Za-z_][A-Za-z0-9_]*)(\*?)')
# A path after a colon is in a git tag (`emulator-oracle:game:event:bridge.c`),
# which is history rather than the tree.
PATH = re.compile(r'(?<![\w.$/*:-])((?:[\w.-]+/)*[\w-]+\.(?:c|h|inc|py|md|toml|tsv|txt|cmake|yml))(?![\w/])')
C_COMMENT_OR_STRING = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|/\*.*?\*/|//[^\n]*', re.S)
TOML_PROSE = re.compile(r'^\s*(?:%s)\s*=\s*"(?:\\.|[^"\\\n])*"' % '|'.join(PROSE_KEYS), re.M)
TOML_PROSE_STRING = re.compile(r'^\s*(?:%s)\s*=\s*"((?:\\.|[^"\\\n])*)"' % '|'.join(PROSE_KEYS), re.M)


def is_code_name(word: str) -> bool:
    """Shaped like a code name rather than a word: an underscore (a leading or
    trailing one makes it a suffix or prefix), or a capital after a lower-case
    letter."""
    if not re.search(r'[A-Za-z]{2}', word):
        return False
    # A capital after a lower-case letter in a word of four letters or more:
    # `KiB` is a unit.
    return '_' in word or len(word) > 3 and bool(re.search(r'[a-z][A-Z]', word))


def cited_names(text: str):
    """[(kind, name)] for the code names in a piece of prose: kind is 'name',
    'prefix' or 'suffix'."""
    out = []
    for mo in NAME.finditer(text):
        lead, word, star = mo.groups()
        if not is_code_name(word):
            continue
        if lead or word.startswith('_'):
            out.append(('suffix', word))
        elif star or word.endswith('_'):
            out.append(('prefix', word))
        else:
            out.append(('name', word))
    return out


def doc_files(root: pathlib.Path):
    files = sorted((root / 'docs').glob('*.md'))
    files += [root / 'CLAUDE.md'] if (root / 'CLAUDE.md').is_file() else []
    files += sorted((root / '.claude' / 'skills').glob('*/SKILL.md'))
    return files


def citations(root: pathlib.Path):
    """{(kind, name): [where]} for every name and path the prose cites; kind is
    'name', 'prefix', 'suffix' or 'path'."""
    found = {}

    def add(kind, name, where):
        found.setdefault((kind, name), []).append(where)

    def lines(text, matches):
        """Each match with its line number, counted as the matches go."""
        line, at = 1, 0
        for mo in matches:
            line += text.count('\n', at, mo.start())
            at = mo.start()
            yield line, mo

    for path in doc_files(root):
        rel = path.relative_to(root).as_posix()
        text = path.read_text(encoding='utf-8')
        for line, mo in lines(text, SPAN.finditer(text)):
            span = mo.group(1)
            for p in PATH.finditer(span):
                add('path', p.group(1), '%s:%d' % (rel, line))
            rest = PATH.sub(' ', span)
            if '://' in rest:
                continue
            for kind, name in cited_names(rest):
                add(kind, name, '%s:%d' % (rel, line))
    ram = root / 're' / 'ram.toml'
    if ram.is_file():
        text = ram.read_text(encoding='utf-8')
        for line, mo in lines(text, TOML_PROSE_STRING.finditer(text)):
            prose = mo.group(1)
            for p in PATH.finditer(prose):
                add('path', p.group(1), 're/ram.toml:%d' % line)
            for kind, name in cited_names(PATH.sub(' ', prose)):
                add(kind, name, 're/ram.toml:%d' % line)
        # The map's comments explain its format with made-up names, so only
        # the files they cite are held to the tree.
        for line, comment in toml_comments_out(text)[1]:
            for p in PATH.finditer(comment):
                add('path', p.group(1), 're/ram.toml:%d' % line)
    return found


def code_files(root: pathlib.Path):
    for name in CODE_FILES:
        if (root / name).is_file():
            yield root / name
    for d in CODE_DIRS:
        for dirpath, dirnames, filenames in os.walk(root / d):
            dirnames[:] = [n for n in dirnames if n != '__pycache__']
            for n in filenames:
                yield pathlib.Path(dirpath) / n


def code_words(root: pathlib.Path):
    """The words of the code, and of its files' names."""
    words = set()
    for path in code_files(root):
        rel = path.relative_to(root).as_posix()
        words.update(WORD.findall(path.stem))
        table = rel.startswith('re/') and path.suffix in RE_TABLES
        if rel in SELF or not table and path.suffix not in CODE_SUFFIXES and path.name != 'CMakeLists.txt':
            continue
        if path.suffix == '.py':
            words.update(python_identifiers(path))
            continue
        text = path.read_text(encoding='utf-8', errors='replace')
        if path.suffix in ('.c', '.h', '.inc'):
            text = C_COMMENT_OR_STRING.sub(lambda mo: ' %s ' % ' '.join(key_string(mo.group(0))), text)
        elif path.suffix == '.toml':
            text = toml_comments_out(TOML_PROSE.sub(' ', text))[0]
        words.update(WORD.findall(text))
    return words


def key_string(literal: str):
    """A string literal that is one identifier (an environment variable, a
    key) names something; a sentence only mentions names."""
    mo = re.fullmatch(r'[rbuRBU]*(["\'])([A-Za-z_][A-Za-z0-9_]*)\1', literal)
    return [mo.group(2)] if mo else []


def python_identifiers(path: pathlib.Path):
    """The identifiers of a Python file's code: its comments, docstrings and
    strings mention names, they don't define them."""
    try:
        with path.open('rb') as f:
            out = set()
            for tok in tokenize.tokenize(f.readline):
                if tok.type == tokenize.NAME:
                    out.add(tok.string)
                elif tok.type == tokenize.STRING:
                    out.update(key_string(tok.string))
            return out
    except (tokenize.TokenError, SyntaxError, UnicodeDecodeError):
        return set(WORD.findall(path.read_text(encoding='utf-8', errors='replace')))


def toml_comments_out(text: str):
    """(the TOML with its `#` comments blanked, [(line, comment)])."""
    out, comments = [], []
    for n, line in enumerate(text.split('\n'), 1):
        quote, i = None, 0
        while i < len(line):
            ch = line[i]
            if quote:
                if ch == '\\':
                    i += 1
                elif ch == quote:
                    quote = None
            elif ch in '"\'':
                quote = ch
            elif ch == '#':
                comments.append((n, line[i + 1:]))
                line = line[:i]
                break
            i += 1
        out.append(line)
    return '\n'.join(out), comments


def all_files(root: pathlib.Path):
    """Every file in the tree outside .git and build output, as relative paths."""
    out = set()
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = pathlib.Path(dirpath).relative_to(root).as_posix()
        if rel_dir == '.':
            dirnames[:] = [n for n in dirnames if n not in ('.git', 'build', 'roms')]
            rel_dir = ''
        dirnames[:] = [n for n in dirnames if n not in ('__pycache__', 'worktrees')]
        for n in filenames:
            out.add(rel_dir + '/' + n if rel_dir else n)
    return out


def exceptions(root: pathlib.Path):
    path = root / EXCEPTIONS
    out = {}
    if path.is_file():
        for line in path.read_text(encoding='utf-8').splitlines():
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            name, _, reason = line.partition('#')
            out[name.strip()] = reason.strip()
    return out


def stale(root: pathlib.Path = ROOT):
    """([(name, [where])] cited but not in the code, [problems with the
    exceptions file])."""
    words = code_words(root)
    files = all_files(root)
    allowed = exceptions(root)
    out, used = [], set()
    for (kind, name), where in sorted(citations(root).items(), key=lambda item: item[0][1]):
        if kind == 'path':
            ok = any(f == name or f.endswith('/' + name) for f in files)
        elif kind == 'prefix':
            ok = any(w.startswith(name) for w in words)
        elif kind == 'suffix':
            ok = any(w.endswith(name) for w in words)
        else:
            ok = name in words
        if ok:
            continue
        if name in allowed:
            used.add(name)
            continue
        out.append((name, where))
    problems = ['%s: %s has no reason on its line' % (EXCEPTIONS, n) for n, r in allowed.items() if not r]
    problems += ['%s: %s is no longer cited or no longer stale; drop its line' % (EXCEPTIONS, n)
                 for n in allowed if n not in used]
    return out, problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--check', action='store_true', help='fail on any stale name')
    args = ap.parse_args(argv)
    started = time.monotonic()
    found, problems = stale()
    for name, where in found:
        shown = ', '.join(where[:4]) + (' and %d more' % (len(where) - 4) if len(where) > 4 else '')
        print('%s: not in the code (%s)' % (name, shown))
    for problem in problems:
        print(problem)
    print('doc-names: %d stale, %d exception problems (%.1fs)' % (len(found), len(problems), time.monotonic() - started))
    return int(bool(args.check and (found or problems)))


if __name__ == '__main__':
    sys.exit(main())
