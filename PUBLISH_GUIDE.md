# Setting up the CHN Package Registry on jsDelivr + GitHub

This guide walks you through creating the GitHub repository that backs the CHN package registry, wiring it to jsDelivr as a CDN, and automating uploads with GitHub Actions.

---

## How it works

Every package you publish with `chn-publish` is uploaded as files into a GitHub repository. jsDelivr then serves those files over its global CDN — no server needed, no cost, instant propagation.

```
chn-publish                      chn-install
    │                                │
    ▼                                ▼
GitHub Repo                   jsDelivr CDN
username/chn-registry   →  cdn.jsdelivr.net/gh/username/chn-registry@main/
    │                                │
    └── registry.json               reads registry.json → finds package
    └── packages/
        └── test/1.0.0/
            ├── CHN-CONF
            ├── manifest.json
            ├── main/test.chn
            └── LICENSE
```

---

## Step 1 — Create the registry repository on GitHub

1. Go to **https://github.com/new**

2. Name it `chn-registry` (or anything you want — you'll configure the name in `chn-config`)

3. Set it to **Public** — jsDelivr only works with public repositories

4. Check **"Add a README file"** (creates the initial commit, required for jsDelivr)

5. Click **Create repository**

6. Clone it locally:
   ```bash
   git clone https://github.com/YOUR_USERNAME/chn-registry
   cd chn-registry
   ```

7. Create the initial `registry.json`:
   ```bash
   echo '{"packages":{}}' > registry.json
   git add registry.json
   git commit -m "init: empty registry"
   git push origin main
   ```

---

## Step 2 — Create a GitHub Personal Access Token

`chn-publish` uses the GitHub API to upload files. You need a token with write access.

1. Go to **https://github.com/settings/tokens/new**

2. Give it a descriptive name: `chn-publish`

3. Set **Expiration** to your preference (90 days or No expiration)

4. Under **Select scopes**, check:
   - `repo` — Full control of private repositories (needed even for public repos to write files via API)

5. Click **Generate token**

6. **Copy the token immediately** — GitHub only shows it once

---

## Step 3 — Configure the CHN toolchain

Run the setup wizard:

```bash
chn-config
```

```
  ⬡  chn-config  v1.0.0
  Configure the CHN package toolchain

── GitHub Registry Setup
  Registry repo []: YOUR_USERNAME/chn-registry

── GitHub Personal Access Token
  GitHub token: ghp_••••••••••••••••••••••••••••••••••••

── Author defaults
  Your full name: Jeck Christopher
  Your email: something@gmail.com

── Testing connection
  ✓  GitHub API authenticated successfully
  ✓  jsDelivr CDN is reachable

  ✓  Configuration saved to: ~/.chn/config
```

Your config is stored in `~/.chn/config`:

```ini
REGISTRY_REPO=YOUR_USERNAME/chn-registry
GITHUB_TOKEN=ghp_yourtoken
AUTHOR_NAME=Jeck Christopher
AUTHOR_EMAIL=something@gmail.com
```

---

## Step 4 — Verify jsDelivr serves your registry

After the first `chn-publish`, wait 5–10 minutes for jsDelivr's cache to populate, then verify:

```bash
curl https://cdn.jsdelivr.net/gh/YOUR_USERNAME/chn-registry@main/registry.json
```

You should see:
```json
{
  "packages": {
    "test": {
      "name": "test",
      "version": "1.0.0",
      ...
    }
  }
}
```

To force jsDelivr to re-fetch immediately (purge cache):

```bash
curl "https://purge.jsdelivr.net/gh/YOUR_USERNAME/chn-registry@main/registry.json"
```

---

## Step 5 — Publish your first package

```bash
# Navigate to your package directory
cd /path/to/testlib

# Publish
chn-publish
```

Full expected output:

```
  ⬡  chn-publish  v1.0.0
  Publish CHN libraries to the registry

── Reading package configuration
  ✓  CHN-CONF is valid
  name                 test
  version              1.0.0
  license              MIT
  launch               main/test.chn
  description          A test source code
  keywords             test tool firstlaunch official

── Checking package files
  ✓  Syntax check passed: main/test.chn
  ✓  Found 3 file(s) to publish
    CHN-CONF
    README.md
    main/test.chn

── Checking CDN
  checking cdn...
  ✓  cdn publisher working

── License information
  To continue we need some information for the LICENSE
  (press Enter to skip any field)

  full name [Jeck Christopher]: Jeck Christopher
  email [something@gmail.com]: something@gmail.com
  date published [today]: 2/26/26
  company name [N/A]:

  ✓  LICENSE generated!
  License type: MIT
  Copyright: Jeck Christopher 2026
  Date: 2/26/26

── Building package manifest
  ✓  Manifest built (5 files)

── Publishing to registry
  uploading to YOUR_USERNAME/chn-registry...

  [████████████████████████████████████████] 100%  Upload complete

  ✓  Registry index updated

  Successfully published:

    ├─  CHN-CONF
    ├─  LICENSE
    ├─  README.md
    ├─  main/test.chn
    ├─  manifest.json

  package              test
  version              1.0.0
  registry             YOUR_USERNAME/chn-registry
  CDN URL              https://cdn.jsdelivr.net/gh/YOUR_USERNAME/chn-registry@main/packages/test/1.0.0/

  Install with:  chn-install test
```

---

## Automating uploads with GitHub Actions

Instead of running `chn-publish` manually, you can trigger it automatically whenever you push a new tag to your package repository.

### In your package repo, create `.github/workflows/publish.yml`:

```yaml
name: Publish to CHN Registry

on:
  push:
    tags:
      - 'v*.*.*'   # triggers on tags like v1.0.0, v2.1.3

jobs:
  publish:
    runs-on: ubuntu-latest
    
    steps:
      - name: Checkout package
        uses: actions/checkout@v4

      - name: Install CHN toolchain
        run: |
          # Download the CHN tools (adjust to wherever you host them)
          git clone https://github.com/YOUR_USERNAME/chn-lang /opt/chn-lang
          echo "/opt/chn-lang/tools" >> $GITHUB_PATH

      - name: Configure chn-publish
        run: |
          mkdir -p ~/.chn
          echo "REGISTRY_REPO=YOUR_USERNAME/chn-registry" >> ~/.chn/config
          echo "GITHUB_TOKEN=${{ secrets.CHN_REGISTRY_TOKEN }}" >> ~/.chn/config
          echo "AUTHOR_NAME=${{ secrets.AUTHOR_NAME }}" >> ~/.chn/config
          echo "AUTHOR_EMAIL=${{ secrets.AUTHOR_EMAIL }}" >> ~/.chn/config

      - name: Update version in CHN-CONF from tag
        run: |
          # Strip the 'v' prefix from the tag (v1.2.3 → 1.2.3)
          VERSION="${GITHUB_REF_NAME#v}"
          sed -i "s/^version:.*/version: $VERSION/" CHN-CONF
          echo "Publishing version: $VERSION"

      - name: Publish package
        run: |
          # Feed author info automatically (non-interactive)
          printf "${{ secrets.AUTHOR_NAME }}\n${{ secrets.AUTHOR_EMAIL }}\n$(date +%m/%d/%y)\n\n" \
            | chn-publish
```

### Required GitHub Secrets

In your package repository, go to **Settings → Secrets and variables → Actions → New repository secret**:

| Secret name | Value |
|---|---|
| `CHN_REGISTRY_TOKEN` | Your GitHub PAT (the same token from Step 2) |
| `AUTHOR_NAME` | Your full name |
| `AUTHOR_EMAIL` | Your email |

### How to trigger a publish

```bash
# Bump CHN-CONF version to 1.1.0, commit, then tag
git add CHN-CONF
git commit -m "bump version 1.1.0"
git tag v1.1.0
git push origin main --tags
```

The GitHub Action fires, updates CHN-CONF with the tag version, and runs `chn-publish` automatically.

---

## Automating the registry repo itself

The registry repo (`chn-registry`) can also have an Action that purges the jsDelivr cache whenever `registry.json` is updated:

### In your `chn-registry` repo, create `.github/workflows/purge-cdn.yml`:

```yaml
name: Purge jsDelivr Cache

on:
  push:
    branches: [main]
    paths:
      - 'registry.json'
      - 'packages/**'

jobs:
  purge:
    runs-on: ubuntu-latest
    steps:
      - name: Purge jsDelivr cache for registry.json
        run: |
          curl -s "https://purge.jsdelivr.net/gh/${{ github.repository }}@main/registry.json"
          echo "Cache purged for registry.json"

      - name: Purge jsDelivr cache for changed packages
        run: |
          # Get list of changed files
          FILES=$(curl -s \
            -H "Authorization: token ${{ secrets.GITHUB_TOKEN }}" \
            "https://api.github.com/repos/${{ github.repository }}/commits/${{ github.sha }}" \
            | python3 -c "
import json, sys
data = json.load(sys.stdin)
for f in data.get('files', []):
    print(f['filename'])
")
          for f in $FILES; do
            URL="https://purge.jsdelivr.net/gh/${{ github.repository }}@main/${f}"
            curl -s "$URL"
            echo "Purged: $f"
          done
```

---

## Directory layout of a published package on GitHub

After publishing `test@1.0.0`, your registry repo looks like:

```
chn-registry/
├── registry.json                    ← master index (updated on every publish)
└── packages/
    └── test/
        └── 1.0.0/
            ├── CHN-CONF             ← original config
            ├── manifest.json        ← file list + metadata
            ├── LICENSE              ← auto-generated
            ├── README.md            ← optional
            └── main/
                └── test.chn         ← the library code
```

`registry.json` structure:

```json
{
  "packages": {
    "test": {
      "name": "test",
      "description": "A test source code",
      "license": "MIT",
      "latest": "1.0.0",
      "versions": ["1.0.0"],
      "keywords": ["test", "tool", "firstlaunch", "official"],
      "published_by": "Jeck Christopher",
      "published_at": "02/26/26"
    }
  }
}
```

---

## Installing a package

```bash
chn-install test
```

Full output:

```
  ⬡  chn-install  v1.0.0

  Checking CDN...
  CDN active
  Finding test in cdn...
  test found!
  start downloading...

  Starting downloading test@1.0.0
  [████████████████████████████████████████] 100%  Downloaded test@1.0.0

  ✓  Successfully downloaded test!
  ▸  Building configuration on test
  ✓  Built configuration on test!
  ▸  Installing test
  ✓  Successfully installed test!

  ✓  Done! Use it with:

    imp test
```

The library is installed to `chn-libs/test.chn` next to your `chn` binary.

---

## Troubleshooting

**jsDelivr returns 404 after publishing**

jsDelivr caches for up to 12 hours. Force a purge:
```bash
curl "https://purge.jsdelivr.net/gh/YOUR_USERNAME/chn-registry@main/registry.json"
```

**GitHub API returns 422 (Unprocessable Entity)**

This usually means the file content is not valid base64, or the SHA for an update is wrong. Try deleting the file from GitHub and publishing again.

**Token permission denied**

Make sure your token has the `repo` scope checked, not just `public_repo`. The GitHub Contents API requires full repo scope even for public repos.

**Package installs but `imp test` fails**

Check that the installed `.chn` file is in the directory `chn-libs/` that the `chn` binary searches. Run `chn-install --info test` to see where it was placed.

**Version already exists**

`chn-publish` will overwrite existing versions in the registry. To protect released versions, consider naming your tags `v1.0.0-final` or implementing a lock in your workflow.

---

## Quick reference

```bash
# First-time setup
chn-config

# Publish a package (run inside package dir)
chn-publish
chn-publish --dry-run     # validate without uploading

# Install packages
chn-install test
chn-install mathlib@2.1.0
chn-install --list
chn-install --info test
chn-install --remove test

# Reset config
chn-config --reset
chn-config --show
```
