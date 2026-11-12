"""The docs' and the RAM map's cited names (tools/doc_names.py).

Each test builds a small tree -- a doc, a RAM map, some code -- and checks what
the check calls stale in it, so a change to what counts as a name, as code or
as a comment fails here rather than quietly passing the docs. The committed
tree passes too, so CI holds the docs to the code without a ROM.
"""
import pathlib
import tempfile
import unittest

from tools import doc_names


class Tree:
    def __init__(self, files):
        self.dir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.dir.name)
        for path, text in files.items():
            p = self.root / path
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text, encoding='utf-8')

    def stale(self):
        found, problems = doc_names.stale(self.root)
        return sorted(name for name, _ in found), problems


CODE = {'game/field/walk.c': '/* walk_the_old_way was here */\n'
                             'void walk_the_leader(void) { }\n'
                             'enum { STEP_SIZE = 2 };\n'}


class TestDocNames(unittest.TestCase):
    def check(self, doc, code=CODE, extra=None):
        files = dict(code)
        files['docs/NOTES.md'] = doc
        files.update(extra or {})
        tree = Tree(files)
        try:
            return tree.stale()
        finally:
            tree.dir.cleanup()

    def test_a_name_the_code_has_passes(self):
        self.assertEqual(self.check('`walk_the_leader` and `STEP_SIZE`\n'), ([], []))

    def test_a_name_the_code_lost_is_stale(self):
        self.assertEqual(self.check('`walk_the_follower` walks\n')[0], ['walk_the_follower'])

    def test_a_comment_does_not_keep_a_name_alive(self):
        self.assertEqual(self.check('see `walk_the_old_way`\n')[0], ['walk_the_old_way'])

    def test_ordinary_words_and_unquoted_names_are_not_names(self):
        self.assertEqual(self.check('`walk`, `$C0:881E`, `-j 2` and walk_the_follower\n'), ([], []))

    def test_camel_case_is_a_name(self):
        self.assertEqual(self.check('`WalkState`\n')[0], ['WalkState'])

    def test_prefixes_and_suffixes(self):
        self.assertEqual(self.check('`walk_the_*`, `walk_*`, `*_leader`, `_the_leader`\n'), ([], []))
        self.assertEqual(self.check('`run_the_*` and `*_follower`\n')[0], ['_follower', 'run_the_'])

    def test_paths_are_checked_as_files(self):
        self.assertEqual(self.check('`field/walk.c`, `game/field/walk.c`, `walk.c`\n'), ([], []))
        self.assertEqual(self.check('`field/run.c`\n')[0], ['field/run.c'])

    def test_a_path_in_a_tag_is_history(self):
        self.assertEqual(self.check('`emulator-oracle:game:field:gone.c`\n'), ([], []))

    def test_the_ram_maps_prose_is_checked_but_its_names_are_code(self):
        ram = ('[[var]]\nname = "field_step_count"\n'
               'meaning = "Steps walk_the_leader takes"\n'
               'source = "game/field/walk.c walk_the_follower $C0:881E; game/field/run.c"\n')
        found = self.check('`field_step_count`\n', extra={'re/ram.toml': ram})[0]
        self.assertEqual(found, ['game/field/run.c', 'walk_the_follower'])

    def test_the_ram_maps_unchecked_reasons_and_comments(self):
        ram = ('# see game/field/gone.h and walk_the_follower\n'
               '[[var]]\nname = "field_step_count"  # game/field/walk.c\n'
               'unchecked = "walk_the_lost reads past it"\n')
        found = self.check('', extra={'re/ram.toml': ram})[0]
        self.assertEqual(found, ['game/field/gone.h', 'walk_the_lost'])

    def test_python_mentions_do_not_define_names(self):
        tool = ('"""walk_the_follower, in a docstring."""\n'
                '# walk_the_lost, in a comment\n'
                'ENV = "CT_WALKER"\n'
                'NOTE = "walk_the_ledger in a sentence"\n'
                'def walk_the_tool(): pass\n')
        found = self.check('`walk_the_follower` `walk_the_lost` `CT_WALKER` `walk_the_ledger` `walk_the_tool`\n',
                           extra={'tools/walker.py': tool, 'tools/walk_baseline.txt': 'walk_the_ledger 1\n'})[0]
        self.assertEqual(found, ['walk_the_follower', 'walk_the_ledger', 'walk_the_lost'])

    def test_an_exception_needs_a_reason_and_a_use(self):
        exc = {'tools/doc_names_exceptions.txt': 'walk_the_follower  # an example of a bad name\n'
                                                 'run_fast\n'}
        found, problems = self.check('`walk_the_follower`\n', extra=exc)
        self.assertEqual(found, [])
        self.assertEqual(len(problems), 2)
        self.assertTrue(any('no reason' in p for p in problems))
        self.assertTrue(any('run_fast' in p and 'no longer' in p for p in problems))

    def test_the_committed_tree_passes(self):
        found, problems = doc_names.stale()
        self.assertEqual(([name for name, _ in found], problems), ([], []))


if __name__ == '__main__':
    unittest.main()
