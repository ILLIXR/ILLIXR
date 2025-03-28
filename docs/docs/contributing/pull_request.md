# Pull Requests

Please follow these steps when making pull requests (PRs):

1. First, create an issue describing the problem that needs to be fixed. If an issue already exists, skip this step. If
   you are looking for an issue to fix, see the ["good first issue" label][E12].

2. Assign the issue to yourself and add appropriate labels. If you are an external contributor, comment on the issue so
   one of the ILLIXR team members can assign the issue to you.

3. Before you start making changes, make a new branch. The branch **MUST** be named
   `issue-<issue number>-<some descriptive name>`. For instance, `issue-32-fix-mem-leak` addresses the memory leak
   described in Issue #32.

4. Fix the issue.

5. Add your name to `ILLIXR/CONTRIBUTORS`.

6. Push commits up to GitHub.

7. Open a PR, and link it to the issue that the PR aims to resolve. Please give the PR a descriptive name.

8. Once you have your PR number, create a PR documentation file. See [here][13] for instructions.

9. As you make progress on your PR, keep your branch up-to-date with the `master` branch which may have been updated
   *after* starting your PR. Your PR **MUST** be updated to reflect changes to `master` in order to be merged. Use the
   following procedure for updating your branch and when you are ready to commit your changes:

    ``` bash
    ## While on your PR branch <issue-branch> hosted at <your-remote> repository:
    git commit # or git stash                                  ## Line A
    git checkout master

    git pull <illixr-remote> master --rebase && git fetch <illixr-remote>   ## Line B

    git checkout <issue-branch>
    git rebase master                                          ## Line C

    ## If you stashed your changes on 'Line A':
    git stash apply <stash-number> && git commit

    git push <your-remote> <issue-branch> --force-with-lease   ## Line D
    ```

    For ILLIXR team members (others jump [here][12]):

    - In the example above, `<illixr-remote>` and `<your-remote>` are the same.

    - When collaborating on branches in our repository, `Line B` may pull in changes that overwrite the git commit
      history when performing `Line C`. Subsequently, performing `Line D` will rewrite the history in the public branch.
      To preserve branch commit histories in the case that a rollback is needed, we will employ a checkpointing process
      for force updated branches. This process will be manually performed, but may be automated in the future.

    If `Line B` shows an update to master, the following example illustrates your local repository just after
    performing `Line B`:

    ``` bash
    A -- B -- C -- P -- Q -- R       ## master
               \
                D -- E -- F          ## issue-123-fixing-bug
    ```

    In this example, commits `P`, `Q`, and `R` have been merged to `master`
    (from feature branches not shown) after feature branch `issue-123-fixing-bug` was
    forked from `master`.

    To checkpoint the `issue-123-fixing-bug` branch while it is checked out:

    ```bash
    git branch issue-123.0-fixing-bug                                   ## Make alias for old issue-123-fixing-bug
    git checkout -b issue-123.1-fixing-bug                              ## Make new branch to rebase with master
    git rebase master                                                   ## Replay issue-123-fixing-bug onto master
    git branch -D issue-123-fixing-bug                                  ## Remove old issue-123-fixing-bug
    git branch issue-123-fixing-bug                                     ## Make issue-123-fixing-bug an alias of new branch
    git push <illixr-remote> issue-123.{0,1}-fixing-bug                 ## Push new checkpointed branches to remote
    git push <illixr-remote> issue-123-fixing-bug --force-with-lease    ## Force update issue-123-fixing-bug
    ```

    !!! note

        The term _alias_ here is used to refer to branches which point to the same commit. This usage is different from standard [Git Aliases][E14] used for git command shortcuts.

    After checkpointing, your local repository should look as follows:

    ``` bash
                               D' -- E' -- F'   ## issue-123.1-fixing-bug, issue-123-fixing-bug
                              /
    A -- B -- C -- P -- Q -- R                  ## master
               \
                D -- E -- F                     ## issue-123.0-fixing-bug
    ```

    Commits `D`, `E`, and `F` have been added to a new branch starting from `R`, but now have been given new hashes.
    This new branch is our up-to-date copy of the feature branch `issue-123-fixing-bug`.

    While working on a checkpointed branch, keep aliases up-to-date using `git rebase`:

    ``` bash
    git commit                               ## Add changes to issue-123.1-fixing-bug
    git checkout issue-123-fixing-bug        ## Switch to main issue-123-fixing-bug branch
    git rebase issue-123.1-fixing-bug        ## Fast-forward issue-123-fixing-bug to issue-123.1-fixing-bug
    ```

    Conflicts are possible when two or more collaborators push changes concurrently to the same branch. As long as
    each collaborator ensures that the branch update process starts at `Line A`, conflicts can be detected and handled
    locally. In other words, _every_ call to `git-push` should be preceded by a call to `git-pull`, following the
    process from `Line A` to `Line D` (or equivalent; git's CLI allows many ways to achieve the same results).

    !!! note

        `Line B` rebases the `master` branch assuming that we have checked out `master`. Forgetting to specify `master` in
        `Line B` may result in a _lossy_ forced update in the example below. Forgetting to checkout `master` will
        immediately apply your checked out feature branch's changes, possibly also resulting in a _lossy_ forced update.

    The output of `Line B` for a collaborator after the checkpointing process may contain something like this:

    ``` bash
    From github.com:ILLIXR/ILLIXR
      A..R          master                  -> <illixr-remote>/master
    + A..F'         issue-123-fixing-bug    -> <illixr-remote>/issue-123-fixing-bug  (forced update)
    * [new branch]  issue-123.0-fixing-bug  -> <illixr-remote>/issue-123.0-fixing-bug
    * [new branch]  issue-123.1-fixing-bug  -> <illixr-remote>/issue-123.1-fixing-bug
    ```

    Conflicts which do not involve updates to the `master` branch can be resolved simply by rebasing the current
    feature branch with the updated feature branch, applying new changes on top of the updated feature branch:

    ``` bash
    ## For the latest checkpoint X (local) and Y (remote), let Z := Y + 1 in
    git checkout issue-123.X-fixing-bug -b issue-123.Z-fixing-bug       ## Make new branch issue-123.Z-fixing-bug
    git rebase <illixr-remote>/issue-123.Y-fixing-bug                   ## Replay updates from issue-123.X-fixing-bug
    git push <illixr-remote> issue-123.Z-fixing-bug                     ## Make sure to update issue-123-fixing-bug after
    ```

    The `--force-with-lease` argument in `Line D` is _not_ required for our new checkpoint branch, since a new branch
    should not conflict with a non-existing remote branch. We _expect_ the subversion number for a new branch
    resulting from our checkpoint conflict resolution to be new and unique. If the push fails, another conflict has
    occurred, and checkpoint conflict resolution should be repeated. `Line D` should be safe to perform for the main
    feature branch now that we have replayed our commits on top of the updated feature branch.

    !!! note

        In the above example, the `git-rebase` is performed using the remote copy of the checkpointed branch.
        We do this because `Line B` will not fast-forward or force update our local branches
        (with the same subversion number as a conflicting remote branch, if any).

    In the case of a conflict with updates to `master`, `Line A` should show updates to both the `master` branch _and_
    the feature branch to be pushed in `Line D`. A checkpointed version of the feature branch may also appear. This is
    because a feature branch should only be checkpointed in the presence of a change to the `master` branch. Forced
    pushes should generally _not_ be used for any other purpose. If multiple updates to `master` and the feature
    branch have occurred, additional checkpointed versions of the feature branch may also appear. In this scenario, we
    need to rebase our latest version of the feature branch with the latest version of the feature branch pulled from
    `<illixr-remote>`.

## PR Documentation

Each PR should create one or more documentation files. These files are used to automatically generate the bulk of the
release notes for each release. The documentation files should go in the directories below `changes`.

- **infrastructure**: this directory is for PRs which deal with changes to base classes (plugin, switchboard, etc.),
  changes to the locations of header files, or other similar system-level changes
- **plugins**: this directory is for major updates to, or creation of, plugins
- **misc**: this directory is for any PR that does not fit into the above two
- The **issues** and **notes** directories are reserved for work at release time and should not be generally used.

The files should be named `pr.XYZ.md` where `XYZ` is replaced by your PR number. For example, `pr.400.md` would refer to
PR #400. The style of the files is [markdown][E15]. The top of the file has a special format

<!--- language: lang-none -->

    ---
    - author.<your_github_name>
    ---

Where `<your_github_name>` should be your GitHub username. This will ensure proper attribution of your work in the
release notes. If there are multiple authors, just add them one, after another, on separate lines.

<!--- language: lang-none -->

    ---
    - author.author1
    - author.author2
    ---

Giving each GitHub username. The rest of the file should be a short description of the PR, generally one or two
sentences at most. If the changes are breaking changes (e.g. restructuring of headers) please start the text with

<!--- language: lang-none -->

    **Breaking**

# Philosophy

Why are the above steps necessary?

1. Assigning the issue to yourself ensures that multiple people don't work on the same thing in parallel.

2. The branch naming scheme organizes things a bit for us, and also makes it easy to find branches.

3. Linking the issue to the PR ensures that we know which issue is being resolved, and also automatically closes the
   issue when the PR gets merged.

4. Using rebases keeps the `master` and feature branch histories streamlined (minimizing branching), thus making it
   easier to compose feature branches for integration testing. See this article on [rebasing public branches][E13] for
   more information.

If your PR has not seen activity from the ILLIXR team after a long period of time (e.g., 2 weeks), feel free to contact
the team directly on the GitHub Issue Conversation tab or at the Gitter forum linked below.

[//]: # (- external -)

[E12]:    https://github.com/ILLIXR/ILLIXR/issues?q=is%3Aopen+is%3Aissue+label%3A%22good+first+issue%22

[E13]:    https://redfin.engineering/git-rebasing-public-branches-works-much-better-than-youd-think-ecc9a115aea9

[E14]:    https://git-scm.com/book/en/v2/Git-Basics-Git-Aliases

[E15]:    https://www.markdownguide.org/

[12]:   pull_request.md#philosophy

[13]:   pull_request.md#pr-documentation
