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
    Use the following procedure for updating your branch and are ready to commit your changes:

    <!--- language: lang-shell -->

        ## While on your PR branch <issue-branch> hosted at <your-remote> repository:
        git commit
        git checkout master
        git pull <illixr-remote> --rebase                           ## Line A
        git checkout <issue-branch>
        git rebase master                                           ## Line B
        git push <your-remote> <issue-branch> --force-with-lease    ## Line C

    If you want to update your branch while you are still making changes to your branch:

    <!--- language: lang-shell -->

        ## While on your PR branch <issue-branch> hosted at <your-remote> repository:
        git stash
        git checkout master
        git pull <illixr-remote> --rebase                           ## Line A
        git checkout <issue-branch>
        git rebase master                                           ## Line B
        git stash apply <stash-number>
        ## Finish your changes
        git commit
        git push <your-remote> <issue-branch> --force-with-lease    ## Line C

    For ILLIXR team members:

    -   In the examples above, `<illixr-remote>` and `<your-remote>` are the same.

    -   When collaborating on branches in our repository, `Line A` may pull in changes that overwrite
            the git commit history when performing `Line B`.
        Subsequently, performing `Line C` will rewrite the history in the public branch.
        To preserve branch commit histories in the case that a rollback is needed, we will employ
            a checkpointing process for force updated branches.
        This process will be manually performed, but may be automated in the future.

        If `Line A` shows an update to master, the following example illustates your local repository
            just after performing `Line A`:

            A -- B -- C -- P -- Q -- R                  ## master
                       \
                        D -- E -- F                     ## issue-123-fixing-bug

        To checkpoint the `issue-123-fixing-bug` while it is checked out:

        <!--- language: lang-shell -->

            git branch issue-123.0-fixing-bug                                   ## Make alias for old issue-123-fixing-bug
            git checkout -b issue-123.1-fixing-bug                              ## Make new branch to rebase with master
            git rebase master                                                   ## Replay issue-123-fixing-bug onto master
            git branch -D issue-123-fixing-bug                                  ## Remove old issue-123-fixing-bug
            git branch issue-123-fixing-bug                                     ## Make issue-123-fixing-bug an alias of new branch
            git push <illixr-remote> issue-123.{0,1}-fixing-bug                 ## Push new checkpointed branches to remote
            git push <illixr-remote> issue-123-fixing-bug --force-with-lease    ## Force update issue-123-fixing-bug

        After checkpointing, your local repository should look as follows:

                                       D' -- E' -- F'   ## issue-123.1-fixing-bug, issue-123-fixing-bug
                                      /
            A -- B -- C -- P -- Q -- R                  ## master
                       \
                        D -- E -- F                     ## issue-123.0-fixing-bug

        While working on a checkpointed branch, keep aliases up-to-date using `git rebase`:

            git commit                          ## Add changes to issue-123.1-fixing-bug
            git checkout issue-123-fixing-bug   ## Switch to main issue-123-fixing-bug branch
            git rebase issue-123.1-fixing-bug   ## Fast-forward issue-123-fixing-bug to issue-123.1-fixing-bug


Why are the above steps necessary?

1.  Assigning the issue to yourself ensures that multiple people don't work on the same thing
        in parallel.

1.  The branch naming scheme organizes things a bit for us, and also makes it easy to find branches.

1.  Linking the issue to the PR ensures that we know which issue is being resolved,
        and also automatically closes the issue when the PR gets merged.

1.  Using rebases keeps the `master` and feature branch histories streamlined (minimizing branching),
        thus making it easier to compose feature branches for integration testing.
    See this article on [rebasing public brances][3] for more information.

If your PR has not seen activity from the ILLIXR team after a long period of time (e.g., 2 weeks),
    feel free to contact the team directly on the GitHub Issue Conversation tab or at
    the Gitter forum linked below.


# Getting Help

You can get seek help from our development community in three places:

1.  [Main documentation site][10]

1.  [API documentation site][11]

1.  [Gitter community forum][1]


[//]: # (- References -)

[1]:    https://gitter.im/ILLIXR/community
[2]:    https://github.com/ILLIXR/ILLIXR/issues?q=is%3Aopen+is%3Aissue+label%3A%22good+first+issue%22
[3]:    https://redfin.engineering/git-rebasing-public-branches-works-much-better-than-youd-think-ecc9a115aea9

[//]: # (- Internal -)

[10]:   index.md
[11]:   api/html/annotated.html
