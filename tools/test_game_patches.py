"""Contracts for game-only patch management after adopting the dependency fork."""
import unittest
from unittest import mock

import game_patches


class GamePatchTests(unittest.TestCase):
    def test_reads_only_game_patch_series(self):
        def stored_files(_repo, *arguments, **_kwargs):
            if arguments[0] == "ls-tree":
                return b"patches/game/02-core.patch\npatches/game/00-include.patch\n"
            return arguments[1].encode()

        with mock.patch.object(game_patches, "run", side_effect=stored_files) as run:
            self.assertEqual(
                game_patches.patches_at("branch"),
                [b"branch:patches/game/00-include.patch", b"branch:patches/game/02-core.patch"],
            )
        self.assertFalse(any("libultraship" in str(call) for call in run.call_args_list))

    def test_accepts_migrated_history(self):
        with mock.patch.object(game_patches, "run", return_value=b""):
            game_patches.check_patch_history("HEAD", "base", "other")

    def test_rejects_legacy_history_before_export_or_merge(self):
        def legacy_history(_repo, *arguments, **_kwargs):
            if arguments[0] == "ls-tree":
                return b"patches/libultraship.patch\n"
            return b"revision\n"

        with mock.patch.object(game_patches, "in_merge", return_value=False), \
                mock.patch.object(game_patches, "run", side_effect=legacy_history) as run, \
                mock.patch.object(game_patches, "export") as export:
            with self.assertRaisesRegex(SystemExit, "legacy libultraship patch"):
                game_patches.merge("old-branch")
            export.assert_not_called()
        self.assertFalse(any(call.args[1] == "merge" for call in run.call_args_list))

    def test_finish_stages_only_game_patches(self):
        with mock.patch.object(game_patches, "in_merge", return_value=True), \
                mock.patch.object(game_patches, "check_patch_history"), \
                mock.patch.object(game_patches, "rejects", return_value=[]), \
                mock.patch.object(game_patches, "export"), \
                mock.patch.object(game_patches, "series", return_value=[]), \
                mock.patch.object(game_patches, "pinned", return_value="game-pin"), \
                mock.patch.object(game_patches, "tree_with") as tree_with, \
                mock.patch.object(game_patches, "run", return_value=b"") as run:
            game_patches.finish()
        run.assert_any_call(game_patches.ROOT, "add", "-A", "--", "patches/game")
        self.assertEqual(tree_with.call_args.args[:3], (game_patches.GAME, "game-pin", []))
        self.assertTrue(all(call.args[0] == game_patches.ROOT for call in run.call_args_list))
        self.assertFalse(any("libultraship" in str(call) for call in run.call_args_list))

    def test_abort_leaves_dependency_untouched(self):
        with mock.patch.object(game_patches, "run", return_value=b"") as run, \
                mock.patch.object(game_patches, "rejects", return_value=[]) as rejects, \
                mock.patch.object(game_patches, "reset") as reset, \
                mock.patch.object(game_patches, "apply") as apply:
            game_patches.abort()
        run.assert_called_once_with(game_patches.ROOT, "merge", "--abort")
        rejects.assert_called_once_with(game_patches.GAME)
        reset.assert_called_once_with()
        apply.assert_called_once_with(False)


if __name__ == "__main__":
    unittest.main()
