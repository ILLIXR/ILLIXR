# Contributing Guidelines

Please follow these steps when making pull requests (PRs):

1. First, create an issue describing the problem that needs to be fixed. If an issue already exists, skip this step. If you are looking for an issue to fix, see the ["good first issue" label](https://github.com/ILLIXR/ILLIXR/issues?q=is%3Aopen+is%3Aissue+label%3A%22good+first+issue%22).
2. Assign the issue to yourself and add appropriate labels.
3. Before you start making changes, make a new branch. The branch **MUST** be named `issue-<issue number>-<some descriptive name>`. For instance, `issue-32-fix-mem-leak` addresses the memory leak described in issue 32.
4. Fix the issue.
5. Push commits up to GitHub.
6. Get [GitHub Hub](https://github.com/github/hub).
7. Open a PR by doing `hub pull-request -i <issue number>` from command line, where `<issue number>` is the number assigned to the issue by GitHub. You should be on the branch that you would like to open the PR with.

Why are the above steps necessary?

1. Assigning the issue to yourself ensures that multiple people don't work on the same thing in parallel.
2. The branch naming scheme organizes things a bit for us, and also makes it easy to find branches.
3. Opening a PR via Hub makes sure that the issue itself is converted into a PR. This is different than linking issues to PRs. While linking is nice, it still means that we have two separate threads of discussion, one on the issue and one on the PR. By converting the issue itself into a PR, everything resides in one place, making it easier to keep track of things.

# Getting Help

You can get help from our development community in three ways:

1. [Main documentation site](https://illixr.github.io/ILLIXR/)
2. [API documentation site](https://illixr.github.io/ILLIXR/api/html/annotated.html)
3. [Gitter](https://gitter.im/ILLIXR/community)
