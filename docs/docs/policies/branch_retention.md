<div style="horiz-align: center; text-align: center">
<img src="../../images/LogoWithHeader.png" alt="Branch Retention Policy"/>

<h2> GitHub Branch Retention Policy </h2>

Version 1.0<P>

March 1, 2024
</div>

### History

| Version | Date       | Who         | Notes           |
|---------|------------|-------------|-----------------|
| 1.0     | 03/01/2024 | ILLIXR Team | Initial version |

## Introduction

The purpose of this document is to outline the policy for retaining, combining, and pruning (deleting) branches in the
ILLIXR GitHub [repository][1].

## Definitions

- **stale**: GitHub’s definition is a branch that has not had any commits in at least 3 months. Since this is an active
  research project that includes students who may not be active for several months at a time (breaks, internships,
  etc.), we will take a looser time definition of 12 months. Note: the master branch can never be considered stale.

- **active**: The inverse of ‘stale’. We will consider a branch to be active if it has had any commits within the last
  12 months.

## Policy

To keep the repository manageable , we will occasionally prune stale branches according to the following:

- Any branch that has been stale for at least 24 months is subject to pruning at any time
- Any branch that is part of an open pull request will be retained, regardless of age/status
- Any branch that is part of an open issue will be retained, regardless of age/status
- Any branch that is part of an official release will be retained until at least the next release
- After an official release, any branches from previous releases should be discussed for pruning

!!! exceptions

    There are instances where someone may want to retain a stale branch indefinitely for research, reference, or other purposes. In these instances, it will be our policy to tag the branch as an archived branch, and then delete the stale branch. The tagged branch will be available indefinitely, but will no longer appear on a listing of branches, thus satisfying the desire to keep a manageable repository and the availability of the code from the branch. The following commands (or their equivalent on the GitHub web interface) will be used:
     ``` bash
     git tag archive/<branch_name> <branch_name>
     git branch -D <branch_name>
     ```
     To checkout the archived branch:
     ``` bash
     git checkout -b <branch_name> archive/<branch_name>
     ```

    !!! note

        The branch name for the tag does not have to match the actual branch name, but should be descriptive of what the branch is for.

In addition to pruning old branches, an active effort should be made to minimize the number of branches used for a
specific development project/plugin (e.g. adding a new capability, handling updates to a 3rd party library, etc.). As a
rule of thumb, each research project and issue should only have a single branch associated with it. Additional branches
can be added for testing/implementing specific details of the project/issue, but should not last more than a few weeks
before being merged back into the primary branch for the project/issue.

Divergent branches will be pruned/merged at release time.


[1]: https://github.com/ILLIXR/ILLIXR
