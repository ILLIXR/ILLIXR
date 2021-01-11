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

Why are the above steps necessary?

1.  Assigning the issue to yourself ensures that multiple people don't work on the same thing
        in parallel.

1.  The branch naming scheme organizes things a bit for us, and also makes it easy to find branches.

1.  Linking the issue to the PR ensures that we know which issue is being resolved,
        and also automatically closes the issue when the PR gets merged.

# Getting Help

You can get seek help from our development community in three places:

1.  [Main documentation site][10]

1.  [API documentation site][11]

1.  [Gitter community forum][1]


[//]: # (- References -)

[1]:    https://gitter.im/ILLIXR/community
[2]:    https://github.com/ILLIXR/ILLIXR/issues?q=is%3Aopen+is%3Aissue+label%3A%22good+first+issue%22

[//]: # (- Internal -)

[10]:   index.md
[11]:   api/html/annotated.html
