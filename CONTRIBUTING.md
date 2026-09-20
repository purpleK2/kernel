# Contributing to purpleK2

Thank you for considering contributing to the purpleK2 project! Please follow the guidelines below to ensure a smooth and consistent contribution process.

## Branches

All features or fixes should be merged to the `meson-rewrite` branch. Do not make changes directly to `main` or any other stable branch. Follow these steps to get started:

1. Fork the repository.
2. Create a new branch from `meson-rewrite` for your changes:
   ```bash
   git checkout meson-rewrite
   git pull origin meson-rewrite
   git checkout -b feat/your-feature-branch
   ```
3. Make your changes in the new branch.

## Code Formatting
There's currently no automatic code formatting tool. Just match the code that's around you, especially when naming variables or functions.

## Documentation

Make sure to document all public (and, when necessary, private) functions thoroughly, using Doxygen syntax. Tell what arguments do, what functions return, if they do, and what those functions do.

## Pull Request

Once your changes are ready:

1. Push your branch to your forked repository:
   ```bash
   git push origin your-feature-branch
   ```
2. Open a pull request targeting the `dev-unstable` branch.
3. Provide a detailed description of your changes, why they're needed, and any relevant information for the reviewers.

## Code Reviews

- All pull requests will be reviewed by project maintainers.
- Please be open to feedback and willing to make adjustments based on the reviewer's comments.

## Additional Guidelines

- Follow the project's coding standards.
- Write [meaningful commit messages](https://www.conventionalcommits.org/en/v1.0.0/).
- If you're adding or changing functionality, please ensure that you do appropriate tests before pushing.
- Respect the code of conduct and contribute respectfully to the project.
