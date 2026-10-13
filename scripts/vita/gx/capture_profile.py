"""Private image profiles require actual framebuffer surface writeback."""

import re


def surface_sync_config(text):
    setting = 'disable-surface-sync: false'
    if re.search(r'^disable-surface-sync:.*$', text, re.MULTILINE):
        return re.sub(r'^disable-surface-sync:.*$', setting, text, flags=re.MULTILINE)
    return text.rstrip('\n') + '\n' + setting + '\n'
