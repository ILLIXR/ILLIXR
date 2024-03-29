# Contributing Guidelines

Please follow these steps when making pull requests (PRs):

1.  First, create an issue describing the problem that needs to be fixed.
    If an issue already exists, skip this step.
    If you are looking for an issue to fix, see the ["good first issue" label][2].

1.  Assign the issue to yourself and add appropriate labels.
    If you are an external contributor, comment on the issue so one of the ILLIXR team members
        can assign the issue to you.

1.  Before you start making changes, make a new branch.
    The branch **MUST** be named `issue-<issue number>-<some descriptive name>`.
    For instance, `issue-32-fix-mem-leak` addresses the memory leak described in Issue #32.

1.  Fix the issue.

1.  Add your name to `ILLIXR/CONTRIBUTORS`.

1.  Push commits up to GitHub.

1.  Open a PR, and link it to the issue that the PR aims to resolve.
    Please give the PR a descriptive name.

1.  As you make progress on your PR, keep your branch up-to-date with the `master` branch which
        may have been updated *after* starting your PR.
    Your PR **MUST** be updated to reflect changes to `master` in order to be merged.
    Use the following procedure for updating your branch and when you are ready to commit your changes:

    <!--- language: lang-none -->

        ## While on your PR branch <issue-branch> hosted at <your-remote> repository:
        git commit # or git stash                                               ## Line A
        git checkout master

        git pull <illixr-remote> master --rebase && git fetch <illixr-remote>   ## Line B

        git checkout <issue-branch>
        git rebase master                                                       ## Line C

        ## If you stashed your changes on 'Line A':
        git stash apply <stash-number> && git commit

        git push <your-remote> <issue-branch> --force-with-lease                ## Line D

    For ILLIXR team members (others jump [here][12]):

    -   In the example above, `<illixr-remote>` and `<your-remote>` are the same.

    -   When collaborating on branches in our repository, `Line B` may pull in changes that overwrite
            the git commit history when performing `Line C`.
        Subsequently, performing `Line D` will rewrite the history in the public branch.
        To preserve branch commit histories in the case that a rollback is needed, we will employ
            a checkpointing process for force updated branches.
        This process will be manually performed, but may be automated in the future.

        If `Line B` shows an update to master, the following example illustrates your local repository
            just after performing `Line B`:

        <!--- language: lang-none -->

            A -- B -- C -- P -- Q -- R                                          ## master
                       \
                        D -- E -- F                                             ## issue-123-fixing-bug

        In this example, commits `P`, `Q`, and `R` have been merged to `master`
            (from feature branches not shown) after feature branch `issue-123-fixing-bug` was
            forked from `master`.

        To checkpoint the `issue-123-fixing-bug` branch while it is checked out:

        <!--- language: lang-none -->

            git branch issue-123.0-fixing-bug                                   ## Make alias for old issue-123-fixing-bug
            git checkout -b issue-123.1-fixing-bug                              ## Make new branch to rebase with master
            git rebase master                                                   ## Replay issue-123-fixing-bug onto master
            git branch -D issue-123-fixing-bug                                  ## Remove old issue-123-fixing-bug
            git branch issue-123-fixing-bug                                     ## Make issue-123-fixing-bug an alias of new branch
            git push <illixr-remote> issue-123.{0,1}-fixing-bug                 ## Push new checkpointed branches to remote
            git push <illixr-remote> issue-123-fixing-bug --force-with-lease    ## Force update issue-123-fixing-bug

        > Note:
        The term _alias_ here is used to refer to branches which point to the same commit.
        This usage is different from standard [Git Aliases][4] used for git command shortcuts.

        After checkpointing, your local repository should look as follows:

        <!--- language: lang-none -->

                                       D' -- E' -- F'                           ## issue-123.1-fixing-bug, issue-123-fixing-bug
                                      /
            A -- B -- C -- P -- Q -- R                                          ## master
                       \
                        D -- E -- F                                             ## issue-123.0-fixing-bug

        Commits `D`, `E`, and `F` have been added to a new branch starting from `R`,
            but now have been given new hashes.
        This new branch is our up-to-date copy of the feature branch `issue-123-fixing-bug`.

        While working on a checkpointed branch, keep aliases up-to-date using `git rebase`:

        <!--- language: lang-none -->

            git commit                                                          ## Add changes to issue-123.1-fixing-bug
            git checkout issue-123-fixing-bug                                   ## Switch to main issue-123-fixing-bug branch
            git rebase issue-123.1-fixing-bug                                   ## Fast-forward issue-123-fixing-bug to issue-123.1-fixing-bug

        Conflicts are possible when two or more collaborators push changes concurrently to
            the same branch.
        As long as each collaborator ensures that the branch update process starts at `Line A`,
            conflicts can be detected and handled locally.
        In other words, _every_ call to `git-push` should be preceeded by a call to `git-pull`,
            following the process from `Line A` to `Line D` (or equivalent; git's CLI allows many
            ways to achieve the same results).

        > Note:
        `Line B` rebases the `master` branch assuming that we have checked out `master`.
        Forgetting to specify `master` in `Line B` may result in a _lossy_ forced update in the
            example below.
        Forgetting to checkout `master` will immediately apply your checked out feature branch's
            changes, possibly also resulting in a _lossy_ forced update.

        The output of `Line B` for a collaborator after the checkpointing process may contain
            something like this:

        <!--- language: lang-none -->

            From github.com:ILLIXR/ILLIXR
              A..R          master                  -> <illixr-remote>/master
            + A..F'         issue-123-fixing-bug    -> <illixr-remote>/issue-123-fixing-bug  (forced update)
            * [new branch]  issue-123.0-fixing-bug  -> <illixr-remote>/issue-123.0-fixing-bug
            * [new branch]  issue-123.1-fixing-bug  -> <illixr-remote>/issue-123.1-fixing-bug

        Conflicts which do not involve updates to the `master` branch can be resolved simply
            by rebasing the current feature branch with the updated feature branch,
            applying new changes on top of the updated feature branch:

        <!--- language: lang-none -->

            ## For the latest checkpoint X (local) and Y (remote), let Z := Y + 1 in
            git checkout issue-123.X-fixing-bug -b issue-123.Z-fixing-bug       ## Make new branch issue-123.Z-fixing-bug
            git rebase <illixr-remote>/issue-123.Y-fixing-bug                   ## Replay updates from issue-123.X-fixing-bug
            git push <illixr-remote> issue-123.Z-fixing-bug                     ## Make sure to update issue-123-fixing-bug after

        The `--force-with-lease` argument in `Line D` is _not_ required for our new checkpoint branch,
            since a new branch should not conflict with a non-existing remote branch.
        We _expect_ the subversion number for a new branch resulting from our
            checkpoint conflict resolution to be new and unique.
        If the push fails, another conflict has occurred, and checkpoint conflict resolution
            should be repeated.
        `Line D` should be safe to perform for the main feature branch now that we have
            replayed our commits on top of the updated feature branch.

        > Note:
        In the above example, the `git-rebase` is performed using the remote copy of
            the checkpointed branch.
        We do this because `Line B` will not fast-forward or force update our local branches
            (with the same subversion number as a conflicting remote branch, if any).

        In the case of a conflict with updates to `master`, `Line A` should show updates to
            both the `master` branch _and_ the feature branch to be pushed in `Line D`.
        A checkpointed version of the feature branch may also appear.
        This is because a feature branch should only be checkpointed in the presence of a
            change to the `master` branch.
        Forced pushes should generally _not_ be used for any other purpose.
        If multiple updates to `master` and the feature branch have occured, additional
            checkpointed versions of the feature branch may also appear.
        In this scenario, we need to rebase our latest version of the feature branch with
            the latest version of the feature branch pulled from `<illixr-remote>`.


# Philosophy

Why are the above steps necessary?

1.  Assigning the issue to yourself ensures that multiple people don't work on the same thing
        in parallel.

1.  The branch naming scheme organizes things a bit for us, and also makes it easy to find branches.

1.  Linking the issue to the PR ensures that we know which issue is being resolved,
        and also automatically closes the issue when the PR gets merged.

1.  Using rebases keeps the `master` and feature branch histories streamlined (minimizing branching),
        thus making it easier to compose feature branches for integration testing.
    See this article on [rebasing public branches][3] for more information.

If your PR has not seen activity from the ILLIXR team after a long period of time (e.g., 2 weeks),
    feel free to contact the team directly on the GitHub Issue Conversation tab or at
    the Gitter forum linked below.


# Other Procedures

1.  Branch Management:

    The branch rebasing and checkpointing process detailed above is tedious, and may be automated in
        the future.
    Check back in with this document occasionally for improvements to the branch management process.

1.  Code Formatting:

    As ILLIXR grows, contributions will need to be standardized to accomodate multiple collaborators
        with different coding styles.
    During code review of a PR, you may be asked to reformat your code to match the standards set for
        ILLIXR code base.
    This process may be manually triggered by a comment from a review, or automated via Git and GitHub
        in the future.

1.  Issue Templates:

    To make collaboration easier, templates for Issues and Pull Requests will be added to
        the GitHub web interface.
    If an appropriate template exists for your task, please ensure to select it before submitting.


# Getting Help

You can get seek help from our development community in three places:

1.  [Main documentation site][10]

1.  [API documentation site][11]

1.  [Gitter community forum][1]


[//]: # (- References -)

[1]:    https://gitter.im/ILLIXR/community
[2]:    https://github.com/ILLIXR/ILLIXR/issues?q=is%3Aopen+is%3Aissue+label%3A%22good+first+issue%22
[3]:    https://redfin.engineering/git-rebasing-public-branches-works-much-better-than-youd-think-ecc9a115aea9
[4]:    https://git-scm.com/book/en/v2/Git-Basics-Git-Aliases

[//]: # (- Internal -)

[10]:   index.md
[11]:   api/html/annotated.html
[12]:   CONTRIBUTING.md#philosophy
