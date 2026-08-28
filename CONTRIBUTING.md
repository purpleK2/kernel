# Contributing to purpleK2

Thank you for considering contributing to the purpleK2 operating system! Please follow the guidelines below to ensure a smooth and consistent contribution process.

## Branches

All changes should be made to the `dev-unstable` branch. Do not make changes directly to the `main` or any other stable branches. Follow these steps to get started:

1. Fork the repository.
2. Create a new branch from `dev-unstable` for your changes:
   ```bash
   git checkout dev-unstable
   git pull origin dev-unstable
   git checkout -b your-feature-branch
   ```
3. Make your changes in the new branch.

## Code Formatting

Before submitting a pull request, please ensure that your code is formatted correctly. We use `clang-format` for consistent code style.

1. Run the following command to format your code:
   ```bash
   ./clang-format.sh
   ```

This will automatically format your code to match the project's style guidelines.

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
- Write meaningful commit messages.
- If you're adding or changing functionality, please ensure that you add appropriate tests.
- Respect the code of conduct and contribute respectfully to the project.
