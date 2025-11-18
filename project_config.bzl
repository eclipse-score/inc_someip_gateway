# This file defines project-specific metadata used by Bazel macros, such as `dash_license_checker`.

# ### 📌 Purpose

# It provides structured configuration that helps determine behavior such as:

# - Source language type (used to determine license check file format)
# - Safety level or other compliance info (e.g. ASIL level)

# ### 📄 Example Content

# ```python
# PROJECT_CONFIG = {
#     "asil_level": "QM",  # or "ASIL-A", "ASIL-B", etc.
#     "source_code": ["cpp", "rust"]  # Languages used in the module
# }
# ```

# ### 🔧 Use Case

# When used with macros like `dash_license_checker`, it allows dynamic selection of file types
#  (e.g., `cargo`, `requirements`) based on the languages declared in `source_code`.

PROJECT_CONFIG = {
    "asil_level": "QM",
    "source_code": ["cpp", "rust"],
}
