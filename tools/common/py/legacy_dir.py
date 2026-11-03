"""The directory of legacy models: one subdirectory per legacy model, which holds its data model and
the platform settings it was released with, under fixed names. The legacy model collector writes it, and the code generator reads it
with --legacy-dir. A hand-kept history of released models has the same format."""

DATAMODEL_FILE = "data-model.json"
"""The data model of a legacy model, in its subdirectory"""

SETTINGS_FILE = "platform-settings.json"
"""The platform settings of a legacy model, in its subdirectory"""
