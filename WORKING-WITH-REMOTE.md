# Working with the GitHub remote

*Where the example workspaces live, how to sign in, clone, pull and push,
and how the GitHub repository and the local bare backup stay in step. The
commands are written out.*

---

## 1. The repositories

| Repository | GitHub | Local bare backup |
|---|---|---|
| `micro-os-scaffold-ctest-examples` (this one): nine test workspaces | `dan-maio/micro-os-scaffold-ctest-examples` | `/home/dan/Downloads/GIT/micro-os-scaffold-ctest-examples.git` |
| `micro-os-scaffold`: the scaffolder that made them | `dan-maio/micro-os-scaffold` | `/home/dan/Downloads/GIT/micro-os-scaffold.git` |
| `micro-os-plus-iii-smp2`: the µOS++ III SMP source (one repository; the four ports are git submodules) | `dan-maio/micro-os-plus-iii-smp2` | none (see its `WORKING-WITH-REMOTE.md`) |

All three are public on GitHub, branch `main`. The working copy is
`/home/dan/Work/micro-os-scaffold-ctest-examples`. `HOW-TO-RUN.md` describes
the workspaces and their results.

---

## 2. The remotes

```bash
git -C /home/dan/Work/micro-os-scaffold-ctest-examples remote -v
origin  https://github.com/dan-maio/micro-os-scaffold-ctest-examples.git  (fetch/push)
local   /home/dan/Downloads/GIT/micro-os-scaffold-ctest-examples.git      (fetch/push)
```

| Remote | What it is | Use |
|---|---|---|
| `origin` | the GitHub repository | the public copy, cloning on another machine |
| `local` | the bare backup in `~/Downloads/GIT/` | fast local backup, offline restore |

`main` tracks `origin/main`. The backup is pushed by name
(`git push local main`). Until 2026-11-24 the bare backup was the only
remote and was called `origin`; it was renamed `local` when GitHub was
added.

---

## 3. Signing in

git reaches GitHub over HTTPS, with the GitHub CLI (`gh`) as credential
helper:

```bash
gh auth status          # Logged in to github.com account dan-maio
gh auth setup-git       # once per machine: git uses gh for github.com
```

`gh auth login` signs in; the token needs the `repo` scope to push. Never
put a token in a remote URL.

---

## 4. Clone and run a workspace

```bash
git clone https://github.com/dan-maio/micro-os-scaffold-ctest-examples.git
cd micro-os-scaffold-ctest-examples/rpi4b-smp
./run.sh list          # build, list the CTest cases
./run.sh               # build, run the selected tests (no hardware cases)
```

A workspace is self-contained and can live anywhere: `run.sh` finds it from
its own location, and the build tree (`micro-os-plus-iii/tests/build/`) is
created on the first run. The tools come from the xPack store
(`~/.local/xPacks`); on another machine, re-scaffold the workspace (section
6) so `scripts/env.sh` points at that machine's tools.

---

## 5. Daily work

```bash
cd /home/dan/Work/micro-os-scaffold-ctest-examples
git pull --ff-only
# ... re-scaffold, add a local test, run, record results ...
git add -A && git commit -m "..."
git push                              # GitHub
git push local main                   # the backup, last
```

Push to GitHub first and to the backup last: if GitHub refuses the push,
the backup still holds what was last published.

### 5.1 What not to commit

`.gitignore` keeps out the build trees (`**/tests/build/`), the build logs,
CTest's temporary files, the scaffolder's stash folder and Python byte code.
Never force them in with `git add -f`:

- a build tree's CMake cache can hold Wi-Fi test credentials
  (`-D WIFI_TEST_SSID=... -D WIFI_TEST_PASS=...`);
- `.elf`, `.bin` and `.img` images are build outputs, rebuilt by `./run.sh`.

Check before a push:

```bash
git diff --cached --name-only | grep -E '/build/|\.(elf|bin|img)$|CMakeCache'   # must print nothing
git log --oneline origin/main..main
```

To check that a value you must not publish is not in the new commits,
without printing it:

```bash
read -rs SECRET
git log -p origin/main..main | grep -c -F -- "$SECRET"   # must print 0
unset SECRET
```

The whole history was searched this way before the first push
(2026-11-24): no Wi-Fi value, token, key or credential file.

### 5.2 The PDFs

`HOW-TO-RUN.pdf`, `README.pdf`, `WORKING-WITH-REMOTE.pdf` and the guides
in the workspaces are rendered from their Markdown with pandoc and
WeasyPrint. Render again in the same commit as the Markdown:

```bash
CSS=/home/dan/Work/micro-os-scaffold/scaffold.css
pandoc HOW-TO-RUN.md -o HOW-TO-RUN.pdf --pdf-engine=weasyprint --css $CSS --metadata pagetitle=HOW-TO-RUN
pandoc README.md     -o README.pdf     --pdf-engine=weasyprint --css $CSS --metadata pagetitle=README
pandoc WORKING-WITH-REMOTE.md -o WORKING-WITH-REMOTE.pdf --pdf-engine=weasyprint --css $CSS --metadata pagetitle=WORKING-WITH-REMOTE
rpi4b-smp/make-stdcxx-pdf.sh                           # STDCXX.pdf
```

---

## 6. Re-scaffolding a workspace

Each workspace's `README.md` ends with the command that regenerates it.
Run it from a clone of `micro-os-scaffold`, then commit the result here:

```bash
cd /home/dan/Work/micro-os-scaffold
node bin/scaffold.js /home/dan/Work/micro-os-scaffold-ctest-examples/rpi4b-smp --yes
```

Without options, it uses the source, the selection and the options recorded
in the workspace's `scaffold.json`. It keeps the local tests and the
`keep` paths of `scripts/local-tests.txt`, and rewrites the generated files
(`README.md`, `run.sh`, `scripts/env.sh`, `scaffold.json`, the copied
kernel and packages). The root `README.pdf` then needs rendering again
(section 5.2).

### 6.1 The source

Since 2026-11-26 the workspaces are scaffolded from the published source,
`https://github.com/dan-maio/micro-os-plus-iii-smp2.git`, and their
`scaffold.json` records it (`sourceUrl`; `sourceRoot` is the clone in the
scaffolder's cache). A re-scaffold without `--source` clones that URL again,
so it works on any machine. Before that they were made from the local folder
`/home/dan/Desktop/micro-os-plu-iii-smp-pr-github`; the kernel, port and
device sources were identical. `micro-os-plus-iii-smp2` holds the four ports
(`aarch32/`, `aarch64/`, `cortexm/`, `posix-arch/`) as git submodules. To
scaffold from a local copy, give `--source` a clone made with `--recursive`:

```bash
git clone --recursive https://github.com/dan-maio/micro-os-plus-iii-smp2.git /home/dan/Work/micro-os-plus-iii-smp2
node bin/scaffold.js /home/dan/Work/micro-os-scaffold-ctest-examples/rpi4b-smp \
  --source /home/dan/Work/micro-os-plus-iii-smp2 --yes
```

or as the URL, which the scaffolder clones, submodules included, into its
cache (`~/.cache/micro-os-scaffold/`):

```bash
node bin/scaffold.js /home/dan/Work/micro-os-scaffold-ctest-examples/rpi4b-smp \
  --source https://github.com/dan-maio/micro-os-plus-iii-smp2.git --yes
```

The scaffolder refuses a local source whose submodule folders are empty (a
clone made without `--recursive`) and prints the fix:
`git -C <source> submodule update --init --recursive`. This needs
`micro-os-scaffold` 6451e708 or later; an older one cloned the URL without
submodules, and the build stopped with "port folder not found".

---

## 7. Restore or move to another machine

From GitHub:

```bash
git clone https://github.com/dan-maio/micro-os-scaffold-ctest-examples.git
git -C micro-os-scaffold-ctest-examples remote add local /home/dan/Downloads/GIT/micro-os-scaffold-ctest-examples.git
```

From the backup, when offline:

```bash
git clone /home/dan/Downloads/GIT/micro-os-scaffold-ctest-examples.git
cd micro-os-scaffold-ctest-examples
git remote rename origin local
git remote add origin https://github.com/dan-maio/micro-os-scaffold-ctest-examples.git
git fetch origin && git branch -u origin/main
```

---

## 8. Cheat sheet

| Task | Command |
|---|---|
| Sign in / check | `gh auth login`, `gh auth status`, `gh auth setup-git` |
| Clone | `git clone https://github.com/dan-maio/micro-os-scaffold-ctest-examples.git` |
| Pull | `git pull --ff-only` |
| Push to GitHub, then the backup | `git push && git push local main` |
| Preview a push | `git push --dry-run` |
| What GitHub does not have yet | `git log --oneline origin/main..main` |
| Compare with GitHub / the backup, no fetch | `git ls-remote origin refs/heads/main`, `git ls-remote local refs/heads/main` |

---

## 9. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `Authentication failed`, `403` | not signed in, or the token lacks `repo` | `gh auth status`, `gh auth login`, `gh auth setup-git` |
| `! [rejected] … (non-fast-forward)` | GitHub has commits you do not | `git pull --ff-only`, or rebase/merge, then push |
| `git push` goes to the backup | an old clone where `origin` is still the backup | rename the remotes (section 7) |
| a workspace does not build on another machine | `scripts/env.sh` points at the old machine's tools | re-scaffold it there (section 6) |
| `submodules not checked out: aarch32/…` when re-scaffolding | the `micro-os-plus-iii-smp2` clone was made without `--recursive` | `git -C <source> submodule update --init --recursive` (section 6.1) |
| a PDF is older than its Markdown | not rendered again | section 5.2 |

---

## 10. State on 2026-11-24

`main` was pushed to both `origin` (GitHub, public) and `local` (the bare
backup), at the same commit, with `main` tracking `origin/main`.
