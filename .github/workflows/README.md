# Automated Release Workflow

Automates main release and hotfix flows via GitHub Actions.

## Workflows

### 1. Component Release (component-release.yml)

Triggered manually from the Actions tab via workflow_dispatch.

#### Inputs

| Input | Required | Description |
|-------|----------|-------------|
| release_version | Yes | Release version (for example: 1.0.0, 1.0.0-rc1) |
| release_type | Yes | main or hotfix |
| release_mode | Yes | approvable or auto-complete (ignored for hotfix) |
| source_branch | Hotfix only | Support branch for hotfix (for example: support/1.0) |

#### Concurrency

Runs are serialized per release key:

- component-release-<release_type>-<release_version>

This prevents two runs for the same version/type from racing on branches/tags.

#### Release Modes

Main release - auto-complete
1. Validates version format.
2. Runs git flow release start -> changelog -> release publish -> release finish.
3. Pushes main, develop, and tags.
4. Deletes remote release/<version> branch after successful finish.

Main release - approvable
1. Validates version format.
2. Runs git flow release start -> changelog -> release publish.
3. Creates a PR: release/<version> -> develop.
4. Stops and waits for PR approval.
5. On approval, the second workflow finishes the release.

Hotfix (always auto-complete)
1. Requires source_branch input.
2. Validates source_branch against ^[A-Za-z0-9._/-]+$.
3. Creates/uses hotfix/<version> from source_branch, updates changelog, merges back into source_branch only.
4. Creates tag <version>.
5. Pushes only source_branch and tag (main/develop are not pushed).

### 2. Component Release Finish On Approval (component-release-finish-on-approval.yml)

Triggered on approved review for release/* PRs targeting develop.

#### What it does
1. Verifies PR review decision is APPROVED.
2. Checks out release branch.
3. Runs git flow release finish (merge to main + develop, create tag).
4. Pushes main, develop, and tags.
5. Deletes remote release/<version> branch.
6. Closes the release PR.

## Authentication and Secrets

Both workflows use:

- RDKCM_DEPLOY_KEY: SSH private key used by checkout and git push operations.
- github.token: built-in ephemeral GitHub Actions token used by gh API/CLI calls.

Why github.token:

- It is automatically provided for each workflow run.
- It avoids introducing a custom repository secret for API access.
- It works with the workflow permissions already declared in the job.

## Failure Cleanup Safety

On failure, cleanup only removes refs created by the current run:

- Deletes tag only if the run created that tag.
- Deletes release/<version> only if the run created that local release branch.
- Deletes hotfix/<version> only if the run created that local hotfix branch.
- Skips cleanup if repository checkout is unavailable.

## Prerequisites

### Repository Settings

- Settings -> Actions -> General -> Workflow permissions: set to Read and write permissions.
- Settings -> Actions -> General: enable Allow GitHub Actions to create and approve pull requests.

### Branch Protection

- For approvable flow: enforce PR review rules on develop.
- For auto-complete flow pushes to develop: ensure the automation actor has appropriate bypass rights in repository rules.

## Version Format

Accepted examples:

- 1.0.0
- 1.0.0-rc1
- 1.0.0v1
- 1.0.0.beta2

Rule: major.minor.patch digits with optional suffix.
