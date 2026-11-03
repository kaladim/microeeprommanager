import os
import sys
import io
import json
import shutil
import tempfile
import unittest
import contextlib
import subprocess
from pathlib import Path
from typing import Dict, List, Optional
from unittest import mock

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.path.dirname(os.path.dirname(__file__)))
import legacy_model_collector
from legacy_model_collector import RECORD_FILE, CollectorError, collect
from common.py.legacy_dir import DATAMODEL_FILE, SETTINGS_FILE

COLLECTOR = os.path.join(os.path.dirname(__file__), "legacy_model_collector.py")
GENERATOR = os.path.join(os.path.dirname(__file__), "..", "meem_config_gen", "meem_config_gen.py")
ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
NEUTRAL_DIR = os.path.join(ROOT, "test", "footprint", "production_like")

DATAMODEL_PATH = "config/meem/eeprom_data_model.json"
SETTINGS_PATH = "config/meem/platform_settings.json"


def datamodel(version: int, **attributes) -> bytes:
    """A data model that differs from those of the other versions. The tool reads no more of it than its attributes."""
    return json.dumps({"name": f"model {version}", "children": [], **attributes}, indent=4).encode("utf-8")


def laid_out(version: int, default: int = 0, **attributes) -> bytes:
    """A data model with blocks: the version gives it an EEPROM layout of its own, and the default changes no layout"""
    parameter = {"name": "p", "children": [], "data_type": 4, "multiplicity": version, "default_value": [default] * version}
    block = {"name": "Block", "children": [parameter], "management_type": 0, "instance_count": 1, "data_recovery_strategy": 1, "compress_defaults": False}
    return json.dumps({"name": f"model {version}", "children": [block], **attributes}, indent=4).encode("utf-8")


def settings(endianness: str = "little") -> bytes:
    return json.dumps({"endianness": endianness}, indent=4).encode("utf-8")


class GitRepository:
    """A temporary git repository, whose commits and tags each get a later date than the one before"""

    def __init__(self, directory: str):
        self.directory = directory
        self.time = 1_700_000_000
        os.makedirs(directory, exist_ok=True)
        self.git("init", "-q")
        # Written at once, since every git process costs time
        with open(os.path.join(directory, ".git", "config"), "a", encoding="utf-8") as f:
            f.write("[user]\n\tname = Test\n\temail = test@example.com\n[core]\n\tautocrlf = false\n[commit]\n\tgpgSign = false\n[tag]\n\tgpgSign = false\n")

    def git(self, *args: str) -> str:
        self.time += 60
        date = f"{self.time} +0000"
        env = os.environ | {"GIT_AUTHOR_DATE": date, "GIT_COMMITTER_DATE": date}
        result = subprocess.run(["git", "-C", self.directory, *args], capture_output=True, env=env)
        if result.returncode != 0:
            raise RuntimeError(f"git {' '.join(args)}: {result.stderr.decode()}")
        return result.stdout.decode().strip()

    def path(self, path: str) -> str:
        return os.path.join(self.directory, *path.split("/"))

    def commit(self, files: Dict[str, Optional[bytes]]) -> str:
        """Writes the files, removes those given as None, and commits them"""
        for path, content in files.items():
            if content is None:
                os.remove(self.path(path))
            else:
                os.makedirs(os.path.dirname(self.path(path)), exist_ok=True)
                with open(self.path(path), "wb") as f:
                    f.write(content)
        self.git("add", "-A")
        self.git("commit", "-q", "--allow-empty", "-m", "commit")
        return self.git("rev-parse", "HEAD")

    def release(self, tag: str, datamodel: bytes, settings: bytes = settings(), annotated: bool = False) -> str:
        """Commits the model files, and tags the commit"""
        commit = self.commit({DATAMODEL_PATH: datamodel, SETTINGS_PATH: settings})
        self.tag(tag, annotated=annotated)
        return commit

    def tag(self, name: str, target: str = "HEAD", annotated: bool = False):
        self.git("tag", *(["-a", "-m", name] if annotated else []), name, target)

    def packed_tag(self, name: str):
        """Tags HEAD in packed-refs, as a clone does: git for Windows can't write a tag whose name a file name can't hold as a loose ref"""
        with open(os.path.join(self.directory, ".git", "packed-refs"), "a", encoding="utf-8", newline="\n") as f:
            f.write(f"{self.git('rev-parse', 'HEAD')} refs/tags/{name}\n")


class CollectorTestCase(unittest.TestCase):
    def setUp(self):
        tmp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(tmp_dir.cleanup)
        self.tmp_dir = tmp_dir.name
        self.repository = GitRepository(os.path.join(self.tmp_dir, "repository"))
        self.output_dir = os.path.join(self.tmp_dir, "build", "legacy_models")

    def collect(self, repository: Optional[GitRepository] = None, tags=(), exclude_tags=(), depth: Optional[int] = None) -> List[legacy_model_collector.Model]:
        """Collects into the output directory, from the current files of the repository, and keeps the messages"""
        repository = repository or self.repository
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            try:
                return collect(repository.path(DATAMODEL_PATH), repository.path(SETTINGS_PATH), self.output_dir, tags, exclude_tags, depth)
            finally:
                self.messages = output.getvalue()

    def subdirectories(self) -> List[str]:
        return sorted(entry.name for entry in os.scandir(self.output_dir) if entry.is_dir())

    def read(self, *path: str) -> bytes:
        with open(os.path.join(self.output_dir, *path), "rb") as f:
            return f.read()

    def record(self) -> dict:
        return json.loads(self.read(RECORD_FILE))

    def tags_of(self, directory: str) -> List[str]:
        return [tag["tag"] for model in self.record()["models"] if model["directory"] == directory for tag in model["tags"]]


class TestCollection(CollectorTestCase):
    def test_each_distinct_release_is_a_model_named_after_its_oldest_tag(self):
        # CRLF and non-ASCII content survive byte for byte
        first = json.dumps({"name": "Größe", "children": []}, indent=4, ensure_ascii=False).encode("utf-8").replace(b"\n", b"\r\n")
        commit_1 = self.repository.release("v1.0", first)
        commit_2 = self.repository.release("v1.1", first, annotated=True)  # A release without a model change
        commit_3 = self.repository.release("v2.0", datamodel(2), settings("big"), annotated=True)

        models = self.collect()

        self.assertEqual([m.directory for m in models], ["v1.0", "v2.0"])
        self.assertEqual(self.subdirectories(), ["v1.0", "v2.0"])
        self.assertEqual(self.read("v1.0", DATAMODEL_FILE), first)
        self.assertEqual(self.read("v1.0", SETTINGS_FILE), settings())
        self.assertEqual(self.read("v2.0", DATAMODEL_FILE), datamodel(2))
        self.assertEqual(self.read("v2.0", SETTINGS_FILE), settings("big"))

        record = self.record()
        self.assertEqual((record["datamodel"], record["settings"]), (DATAMODEL_PATH, SETTINGS_PATH))
        self.assertEqual(
            record["models"],
            [
                {"directory": "v1.0", "tags": [{"tag": "v1.0", "commit": commit_1}, {"tag": "v1.1", "commit": commit_2}]},
                {"directory": "v2.0", "tags": [{"tag": "v2.0", "commit": commit_3}]},
            ],
        )
        self.assertIn("v1.1: the same files as v1.0.", self.messages)

    def test_the_oldest_tag_is_the_first_created_not_the_first_by_name(self):
        self.repository.release("b-first", datamodel(1))
        self.repository.release("a-second", datamodel(1))

        self.assertEqual([m.directory for m in self.collect()], ["b-first"])

    def test_the_files_are_read_from_the_tags_not_from_the_working_tree(self):
        self.repository.release("v1.0", datamodel(1))
        self.repository.commit({DATAMODEL_PATH: datamodel(2)})  # The current model, not released

        self.collect()

        self.assertEqual(self.subdirectories(), ["v1.0"])
        self.assertEqual(self.read("v1.0", DATAMODEL_FILE), datamodel(1))

    def test_a_tag_that_lacks_a_file_is_ignored(self):
        self.repository.commit({"README.md": b"before the model"})
        self.repository.tag("v0.1")
        self.repository.commit({DATAMODEL_PATH: datamodel(1)})
        self.repository.tag("v0.2")  # Without the settings
        self.repository.release("v1.0", datamodel(1))

        self.assertEqual([m.directory for m in self.collect()], ["v1.0"])
        self.assertIn(f"v0.1: ignored, it has no {DATAMODEL_PATH} and {SETTINGS_PATH}.", self.messages)
        self.assertIn(f"v0.2: ignored, it has no {SETTINGS_PATH}.", self.messages)

    def test_a_release_of_mEEM_v1_is_skipped(self):
        self.repository.release("v1.0", datamodel(1, checksum_size=2))
        self.repository.release("v2.0", datamodel(2))

        self.assertEqual([m.directory for m in self.collect()], ["v2.0"])
        self.assertIn("v1.0: skipped, a release of mEEM v1", self.messages)

    def test_a_data_model_that_isnt_valid_json_is_skipped_with_a_warning(self):
        self.repository.release("broken", b"{ not JSON")
        self.repository.release("not-an-object", b"[1, 2]")
        self.repository.release("v1.0", datamodel(1))

        self.assertEqual([m.directory for m in self.collect()], ["v1.0"])
        self.assertIn("warning:: broken: skipped", self.messages)
        self.assertIn("warning:: not-an-object: skipped", self.messages)

    def test_tags_of_trees_and_blobs_are_ignored_without_a_word(self):
        self.repository.release("v1.0", datamodel(1))
        self.repository.tag("tree", "HEAD^{tree}")
        self.repository.tag("blob", f"HEAD:{DATAMODEL_PATH}")
        self.repository.tag("annotated-blob", f"HEAD:{DATAMODEL_PATH}", annotated=True)

        self.assertEqual([m.directory for m in self.collect()], ["v1.0"])
        self.assertNotIn("blob", self.messages)
        self.assertNotIn("tree", self.messages)

    def test_globs_select_the_tags(self):
        for n, tag in enumerate(["v1.0", "v2.0", "rc/2.1", "other"]):
            self.repository.release(tag, datamodel(n))

        cases = [
            ((), (), ["v1.0", "v2.0", "rc_2.1", "other"]),
            (["v*"], (), ["v1.0", "v2.0"]),
            (["v*", "rc/*"], (), ["v1.0", "v2.0", "rc_2.1"]),
            (["v*"], ["v1.*"], ["v2.0"]),
            ((), ["rc/*", "other"], ["v1.0", "v2.0"]),
        ]
        for tags, exclude_tags, expected in cases:
            with self.subTest(tags=tags, exclude_tags=exclude_tags):
                self.assertEqual([m.directory for m in self.collect(tags=tags, exclude_tags=exclude_tags)], expected)
                self.assertEqual((self.record()["tags"], self.record()["exclude_tags"]), (list(tags), list(exclude_tags)))

    def test_names_that_a_windows_file_name_cant_hold_or_that_collide(self):
        self.repository.release("release/2.0", datamodel(1))
        self.repository.release("release_2.0", datamodel(2))
        self.repository.commit({DATAMODEL_PATH: datamodel(3)})
        self.repository.packed_tag('a"b<c>d|e')
        self.repository.release(RECORD_FILE, datamodel(4))

        self.assertEqual([m.directory for m in self.collect()], ["release_2.0", "release_2.0-2", "a_b_c_d_e", f"{RECORD_FILE}-2"])
        self.assertEqual(self.read("release_2.0-2", DATAMODEL_FILE), datamodel(2))


class TestDepth(CollectorTestCase):
    def directories(self, **arguments) -> List[str]:
        return [model.directory for model in self.collect(**arguments)]

    def test_without_a_depth_every_model_is_kept(self):
        self.repository.release("v1.0", laid_out(1))
        self.repository.release("v1.1", laid_out(1, default=7))  # The same layout as v1.0
        self.repository.release("v2.0", laid_out(2))  # The current files

        self.assertEqual(self.directories(), ["v1.0", "v1.1", "v2.0"])
        self.assertEqual(self.record()["depth"], "all")

    def test_the_depth_keeps_the_newest_models_and_the_directory_follows_it(self):
        for version in (1, 2, 3):
            self.repository.release(f"v{version}.0", laid_out(version))
        self.repository.commit({DATAMODEL_PATH: laid_out(4)})  # The current model, not released

        self.assertEqual(self.directories(depth=2), ["v2.0", "v3.0"])
        self.assertEqual(self.subdirectories(), ["v2.0", "v3.0"])
        self.assertEqual(self.record()["depth"], 2)
        self.assertEqual([m["directory"] for m in self.record()["models"]], ["v2.0", "v3.0"])
        self.assertEqual(self.read("v3.0", DATAMODEL_FILE), laid_out(3))
        self.assertIn("v1.0: left out, it's behind the depth of 2.", self.messages)

        self.assertEqual(self.directories(depth=1), ["v3.0"])
        self.assertEqual(self.subdirectories(), ["v3.0"])

        self.assertEqual(self.directories(depth=10), ["v1.0", "v2.0", "v3.0"])
        self.assertEqual(self.subdirectories(), ["v1.0", "v2.0", "v3.0"])

    def test_tags_without_a_valid_model_dont_count(self):
        self.repository.release("old", laid_out(1))
        self.repository.release("new", laid_out(2))
        self.repository.release("mEEM-v1", laid_out(3, checksum_size=2))
        self.repository.release("broken", b"{ not JSON")
        self.repository.commit({SETTINGS_PATH: None})
        self.repository.tag("no-settings")
        self.repository.commit({DATAMODEL_PATH: laid_out(4), SETTINGS_PATH: settings()})

        self.assertEqual(self.directories(depth=2), ["old", "new"])

    def test_the_tags_of_a_model_count_once(self):
        self.repository.release("v1.0", laid_out(1))
        for tag in ("v2.0", "v2.1", "v2.2"):
            self.repository.release(tag, laid_out(2))
        self.repository.commit({DATAMODEL_PATH: laid_out(3)})

        self.assertEqual(self.directories(depth=2), ["v1.0", "v2.0"])
        self.assertEqual(self.tags_of("v2.0"), ["v2.0", "v2.1", "v2.2"])

    def test_a_model_is_as_new_as_its_newest_tag(self):
        self.repository.release("v1.0", laid_out(1))
        self.repository.release("v2.0", laid_out(2))
        self.repository.release("v3.0", laid_out(1))  # Back to the model of v1.0
        self.repository.commit({DATAMODEL_PATH: laid_out(3)})

        self.assertEqual(self.directories(depth=1), ["v1.0"])
        self.assertEqual(self.tags_of("v1.0"), ["v1.0", "v3.0"])

    def test_a_build_at_a_release_tag_doesnt_spend_the_depth_on_itself(self):
        self.repository.release("v1.0", laid_out(1))
        self.repository.release("v2.0", laid_out(2))  # The current files

        self.assertEqual(self.directories(depth=1), ["v1.0"])
        self.assertIn("v2.0: left out, it has the same EEPROM layout as the current model.", self.messages)

    def test_a_release_with_the_layout_of_the_current_model_doesnt_count(self):
        self.repository.release("v1.0", laid_out(1))
        self.repository.release("v2.0", laid_out(2))
        self.repository.commit({DATAMODEL_PATH: laid_out(2, default=7)})

        self.assertEqual(self.directories(depth=1), ["v1.0"])

    def test_releases_with_the_same_layout_count_once(self):
        self.repository.release("v1.0", laid_out(1))
        self.repository.release("v2.0", laid_out(2))
        self.repository.release("v2.1", laid_out(2, default=7))
        self.repository.commit({DATAMODEL_PATH: laid_out(3)})

        self.assertEqual(self.directories(depth=2), ["v1.0", "v2.1"])
        self.assertIn("v2.0: left out, it has the same EEPROM layout as v2.1.", self.messages)

    def test_a_layout_is_that_of_the_models_own_settings(self):
        self.repository.release("v1.0", laid_out(1), settings("big"))
        self.repository.release("v2.0", laid_out(1), settings("little"))  # The current files

        self.assertEqual(self.directories(depth=1), ["v1.0"])

    def test_a_model_without_a_layout_counts_and_is_left_to_the_generator(self):
        self.repository.release("v1.0", laid_out(1))
        self.repository.release("no-layout", json.dumps({"children": [{"no": "block"}]}).encode("utf-8"))
        self.repository.commit({DATAMODEL_PATH: laid_out(2)})

        self.assertEqual(self.directories(depth=1), ["no-layout"])
        self.assertEqual(self.directories(depth=1, exclude_tags=["no-layout"]), ["v1.0"])

    def test_the_depth_applies_to_the_tags_of_the_globs(self):
        for version, tag in enumerate(["v1.0", "v2.0", "rc/3.0"], start=1):
            self.repository.release(tag, laid_out(version))
        self.repository.commit({DATAMODEL_PATH: laid_out(4)})

        self.assertEqual(self.directories(depth=1), ["rc_3.0"])
        self.assertEqual(self.directories(depth=1, tags=["v*"]), ["v2.0"])
        self.assertEqual(self.directories(depth=1, tags=["v*"], exclude_tags=["v2.*"]), ["v1.0"])

    def test_a_model_keeps_its_name_whatever_the_depth(self):
        self.repository.release("release/2.0", laid_out(1))
        self.repository.release("release_2.0", laid_out(2))
        self.repository.commit({DATAMODEL_PATH: laid_out(3)})

        self.assertEqual(self.directories(depth=1), ["release_2.0-2"])
        self.assertEqual(self.read("release_2.0-2", DATAMODEL_FILE), laid_out(2))


class TestDirectory(CollectorTestCase):
    def test_an_unchanged_history_changes_no_file(self):
        self.repository.release("v1.0", datamodel(1))
        self.repository.release("v2.0", datamodel(2))
        self.collect()

        files = [os.path.join(root, name) for root, _, names in os.walk(self.output_dir) for name in names]
        for path in files:
            os.utime(path, ns=(1_000_000_000, 1_000_000_000))

        self.collect()

        self.assertEqual(len(files), 5)
        for path in files:
            self.assertEqual(os.stat(path).st_mtime_ns, 1_000_000_000, path)

    def test_a_changed_file_is_written_again(self):
        self.repository.release("v1.0", datamodel(1))
        self.collect()
        with open(os.path.join(self.output_dir, "v1.0", DATAMODEL_FILE), "wb") as f:
            f.write(b"changed")

        self.collect()

        self.assertEqual(self.read("v1.0", DATAMODEL_FILE), datamodel(1))

    def test_a_model_that_isnt_collected_any_more_is_removed(self):
        self.repository.release("v1.0", datamodel(1))
        self.repository.release("v2.0", datamodel(2))
        self.collect()
        self.assertEqual(self.subdirectories(), ["v1.0", "v2.0"])

        self.repository.git("tag", "-d", "v2.0")
        self.collect()

        self.assertEqual(self.subdirectories(), ["v1.0"])
        self.assertEqual([m["directory"] for m in self.record()["models"]], ["v1.0"])

    def test_no_tag_gives_an_empty_directory(self):
        self.repository.commit({DATAMODEL_PATH: datamodel(1), SETTINGS_PATH: settings()})

        self.assertEqual(self.collect(), [])
        self.assertEqual(self.subdirectories(), [])
        self.assertEqual(self.record()["models"], [])
        self.assertIn("No tag matches", self.messages)
        self.assertIn("0 legacy model(s) from 0 tag(s)", self.messages)

    def test_an_empty_directory_is_taken(self):
        self.repository.release("v1.0", datamodel(1))
        os.makedirs(self.output_dir)

        self.collect()

        self.assertEqual(self.subdirectories(), ["v1.0"])

    def test_a_directory_that_isnt_the_tools_is_refused_and_left_as_it_was(self):
        self.repository.release("v1.0", datamodel(1))
        os.makedirs(os.path.join(self.output_dir, "something"))
        with open(os.path.join(self.output_dir, "notes.txt"), "w") as f:
            f.write("keep")

        with self.assertRaisesRegex(CollectorError, "isn't the tool's directory"):
            self.collect()

        self.assertEqual(sorted(os.listdir(self.output_dir)), ["notes.txt", "something"])


class TestFailures(CollectorTestCase):
    def test_files_outside_a_git_repository_are_an_error(self):
        plain = GitRepository.__new__(GitRepository)
        plain.directory = os.path.join(self.tmp_dir, "plain")
        os.makedirs(os.path.join(plain.directory, "config", "meem"))
        for path, content in ((DATAMODEL_PATH, datamodel(1)), (SETTINGS_PATH, settings())):
            with open(plain.path(path), "wb") as f:
                f.write(content)

        # Keeps git from finding a repository above the temporary directory
        with mock.patch.dict(os.environ, {"GIT_CEILING_DIRECTORIES": self.tmp_dir}):
            with self.assertRaisesRegex(CollectorError, "isn't in a git repository"):
                self.collect(plain)
        self.assertFalse(os.path.exists(self.output_dir))

    def test_a_missing_file_is_an_error(self):
        self.repository.commit({DATAMODEL_PATH: datamodel(1)})

        with self.assertRaisesRegex(CollectorError, "doesn't exist"):
            self.collect()

    def test_files_in_different_repositories_are_an_error(self):
        self.repository.commit({DATAMODEL_PATH: datamodel(1)})
        other = GitRepository(os.path.join(self.tmp_dir, "other"))
        other.commit({SETTINGS_PATH: settings()})

        with self.assertRaisesRegex(CollectorError, "different repositories"):
            collect(self.repository.path(DATAMODEL_PATH), other.path(SETTINGS_PATH), self.output_dir)

    def test_git_that_isnt_installed_is_an_error(self):
        self.repository.release("v1.0", datamodel(1))

        with mock.patch.object(legacy_model_collector, "GIT", "git-that-isnt-installed"):
            with self.assertRaisesRegex(CollectorError, "wasn't found"):
                self.collect()

    def test_a_shallow_clone_is_a_warning(self):
        self.repository.release("v1.0", datamodel(1))
        self.repository.release("v2.0", datamodel(2))
        clone = os.path.join(self.tmp_dir, "clone")
        subprocess.run(["git", "clone", "-q", "--depth", "1", Path(self.repository.directory).as_uri(), clone], check=True, capture_output=True)
        shallow = GitRepository.__new__(GitRepository)
        shallow.directory = clone

        self.collect(shallow)

        self.assertIn("warning:: ", self.messages)
        self.assertIn("is a shallow clone", self.messages)


class TestCommandLine(CollectorTestCase):
    def run_collector(self, *args: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, COLLECTOR, self.repository.path(DATAMODEL_PATH), self.repository.path(SETTINGS_PATH), self.output_dir, *args],
            capture_output=True,
            text=True,
        )

    def test_exit_codes(self):
        self.repository.release("v1.0", datamodel(1))
        self.repository.release("v2.0", datamodel(2))

        result = self.run_collector("--tags", "v*", "--exclude-tags", "v1.*")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.subdirectories(), ["v2.0"])

        shutil.rmtree(self.output_dir)
        os.makedirs(self.output_dir)
        with open(os.path.join(self.output_dir, "notes.txt"), "w") as f:
            f.write("keep")

        result = self.run_collector()
        self.assertEqual(result.returncode, 1)
        self.assertIn("legacy_model_collector::error:: ", result.stdout)
        self.assertNotIn("Traceback", result.stdout + result.stderr)

    def test_depth(self):
        self.repository.release("v1.0", laid_out(1))
        self.repository.release("v2.0", laid_out(2))
        self.repository.commit({DATAMODEL_PATH: laid_out(3)})

        result = self.run_collector("--depth", "1")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.subdirectories(), ["v2.0"])

        result = self.run_collector("--depth", "all")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.subdirectories(), ["v1.0", "v2.0"])

    def test_a_depth_that_isnt_a_positive_number_or_all_is_refused(self):
        self.repository.release("v1.0", laid_out(1))

        for value in ("0", "-1", "1.5", "some", ""):
            with self.subTest(value=value):
                result = self.run_collector("--depth", value)
                self.assertEqual(result.returncode, 2)
                self.assertIn("--depth", result.stderr)
                self.assertNotIn("Traceback", result.stderr)
                self.assertFalse(os.path.exists(self.output_dir))


class TestGeneratorReadsTheDirectory(CollectorTestCase):
    """End to end: the neutral model released as v1.0, then the model with Block09 changed as the current model"""

    def read_neutral(self, name: str) -> bytes:
        with open(os.path.join(NEUTRAL_DIR, name), "rb") as f:
            return f.read()

    def test_the_generator_migrates_from_the_collected_release(self):
        self.repository.release("v1.0", self.read_neutral("eeprom_datamodel.json"), self.read_neutral("platform_settings_xc8.json"))
        self.repository.commit({DATAMODEL_PATH: self.read_neutral("eeprom_datamodel_migration.json")})
        self.collect()

        self.assert_generator_migrates_from_v1_0()

    def test_the_depth_is_what_the_generator_migrates_from(self):
        """The release before the current model holds the depth, though two newer tags have the current layout: one with
        the current files, and one with another description, which the generator tells apart only by the layout"""
        current = self.read_neutral("eeprom_datamodel_migration.json")
        released = json.loads(current)
        released["description"] = "Another description, the same layout"

        self.repository.release("v1.0", self.read_neutral("eeprom_datamodel.json"), self.read_neutral("platform_settings_xc8.json"))
        self.repository.release("v2.0", json.dumps(released, indent=4).encode("utf-8"), self.read_neutral("platform_settings_xc8.json"))
        self.repository.release("v2.1", current, self.read_neutral("platform_settings_xc8.json"))
        self.collect(depth=1)

        self.assertEqual(self.subdirectories(), ["v1.0"])
        self.assert_generator_migrates_from_v1_0()

    def assert_generator_migrates_from_v1_0(self):
        generated_dir = os.path.join(self.tmp_dir, "generated")
        os.makedirs(generated_dir)
        result = subprocess.run(
            [
                sys.executable,
                GENERATOR,
                self.repository.path(DATAMODEL_PATH),
                self.repository.path(SETTINGS_PATH),
                generated_dir,
                "--legacy-dir",
                self.output_dir,
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        with open(os.path.join(generated_dir, "MEEM_Migration.h"), encoding="utf-8") as f:
            self.assertIn("#define MEEM_LEGACY_MODEL_COUNT    1u\n", f.read())
        with open(os.path.join(generated_dir, "MEEM_Migration.c"), encoding="utf-8") as f:
            self.assertIn(f"/* v1.0/{DATAMODEL_FILE} */", f.read())


if __name__ == "__main__":
    unittest.main()
