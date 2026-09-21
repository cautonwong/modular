# Pinned third-party dependencies

**A tag is not a pin.** A git tag can be moved, and a `FetchContent_Declare` that
names only a tag is a build whose inputs can change without its source changing. This
page is the policy; `ci/dependencies.json` is the record, and two checks make the
record load-bearing.

## How it is enforced, in two places

| where | what it catches |
|---|---|
| `check_dependencies.py` (CI, pre-commit) | the shape (40-hex commit, tag, url, licence, an existing `used_by`), duplicates, an empty record - and **that the CMake pin each entry names carries exactly that commit**, so the manifest and the build cannot drift apart |
| the fetching port (`pal/rtos/freertos/CMakeLists.txt`), at configure time | the thing the exercise is about: the **resolved checkout** compared against the pinned commit. A tag that moved after the fact, or a pin edited in one place only, fails the configure with `FreeRTOS-Kernel revision drift` |

The fetch is still **by tag and shallow** (fast), and the revision is verified
afterwards - which is why the tag is recorded next to the commit rather than
replaced by it. Fetching by commit would force a full clone on every CI run.

## Bumping a dependency

1. Edit `ci/dependencies.json`: `version`, `commit`, and `tag_object` (present when
   the tag is annotated - it is what `git ls-remote` returns).
2. Edit the CMake variable the entry names. **They must agree**; the checker fails
   otherwise, by design: two records that disagree are worse than one.
3. Add a line to the entry's `note` if the reasoning changed (why this revision).
4. Build the affected product, run its smoke, and run the gates below.

The `owner` field says who reviews it. Today every entry is owned by `platform`.

## What a bump has to pass

- `check_dependencies.py` - immutability and agreement;
- the configure-time revision check, which runs in every build that fetches the
  component;
- the four firmware gates (size, map budget, stack usage, zero dynamic memory) and
  the QEMU smoke for the affected product;
- for a kernel specifically: the RTOS smoke **and** the starvation probe in both
  directions (`docs/rtos-runner.md`), because a scheduling change is exactly what a
  kernel bump can bring;
- the SBOM and provenance regenerate with the new component. The generators are
  covered by `tests/guards/run_generator_selftest.py`, and CI uploads both documents
  as artifacts next to the firmware.

## Rollback

Revert the two lines. The **commit** pin is what makes the rollback reliable: the
previous revision stays fetchable even if the tag is later moved or deleted. That is
the whole reason the commit, not the tag, is the truth.

## Dependabot, and the automated path

`.github/dependabot.yml` covers **GitHub Actions only**: Dependabot has no
CMake-`FetchContent` ecosystem, so a kernel bump is a manual PR. Both paths follow
the same rule - **the manifest is the single review surface**. A change that bumps a
pin without touching `ci/dependencies.json` fails the checker, which is what makes
"automated or manual" a detail rather than a policy.

## Named gaps

- **No upstream watch.** Nothing notices that a new release exists; a scheduled job
  could diff the manifest against upstream tags. Named here instead of implied.
- **No content hashes.** The commit is recorded; CycloneDX also supports hashes of
  the fetched sources, and we do not record them.
- **Only one component so far.** That is the point of doing this before ThreadX and
  Zephyr (#134, #82): each adds a manifest entry, and the entry is what the SBOM,
  the provenance and the review all read.
