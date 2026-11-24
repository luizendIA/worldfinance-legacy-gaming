"""Offline consistency checks for the first published dataset and static pages."""
import json
import math
import re
import statistics
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit

from summarize import summary


ROOT = Path(__file__).resolve().parents[1]


class Page(HTMLParser):
    def __init__(self, path):
        super().__init__()
        self.path = path
        self.ids = set()
        self.links = []
        self.language = None
        self.feed(path.read_text(encoding="utf-8"))

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            assert attrs["id"] not in self.ids, ("duplicate id", self.path, attrs["id"])
            self.ids.add(attrs["id"])
        if tag == "html":
            self.language = attrs.get("lang")
        for key in ("href", "src"):
            if key in attrs:
                self.links.append(attrs[key])


def check_pages():
    folder = ROOT / "docs"
    for filename, language in (("index.html", "en"), ("pt.html", "pt-BR")):
        page = Page(folder / filename)
        assert page.language == language
        assert {"results", "hardware", "contribute", "support"} <= page.ids
        for link in page.links:
            target = urlsplit(link)
            if target.scheme:
                assert target.scheme == "https" and target.hostname, link
            elif target.path:
                assert (folder / target.path).is_file(), link
            elif target.fragment:
                assert target.fragment in page.ids, link


def check_captures():
    folder = ROOT / "benchmarks" / "rx570-2026-11-24"
    captures = [json.loads((folder / f"run-{i}.json").read_text(encoding="utf-8"))
                for i in range(1, 5)]
    times = {"native": [], "scaled": []}
    for i, run in enumerate(captures):
        assert not run["software"] and "RX 570" in run["adapter"]
        assert run["lower_resolution_first"] == bool(i % 2)
        assert run["warmups_per_profile"] == 8
        assert 0 < run["rgb_rmse_8bit"] < 255
        assert run["d3d12"] == {"device": True, "mesh_tier": 0, "shader_model": "6.5"}
        stats = summary(run)
        for name, dims in (("native", (1280, 720)), ("scaled", (640, 360))):
            profile = run[name]
            assert (profile["width"], profile["height"]) == dims
            assert profile["scene_color_bytes"] == math.prod(dims) * 4
            assert stats[name]["count"] == 32
            times[name].append(stats[name]["median_ms"])
    values = [f"{statistics.median(group):.3f}" for group in times.values()]
    for filename, separator in (("index.html", "."), ("pt.html", ",")):
        html = (ROOT / "docs" / filename).read_text(encoding="utf-8")
        for value in values:
            assert value.replace(".", separator) + " ms" in html


def check_markdown_links():
    for path in ROOT.rglob("*.md"):
        if any(part == ".git" or part.startswith("build") for part in path.relative_to(ROOT).parts[:-1]):
            continue
        content = path.read_text(encoding="utf-8")
        for link in re.findall(r"\[[^\]]*\]\(([^)\s]+)\)", content):
            target = urlsplit(link)
            if not target.scheme and target.path:
                assert (path.parent / target.path).exists(), (path, link)


if __name__ == "__main__":
    check_pages()
    check_captures()
    check_markdown_links()
    print("Verified both languages, local references and four hardware captures.")
