#pragma once

// Keep narrow and wide literals available for format strings and Windows UI.
#define KL1_RELEASE_VERSION "0.3.67"
#define KL1_RELEASE_WIDEN_LITERAL(value) L##value
#define KL1_RELEASE_WIDEN(value) KL1_RELEASE_WIDEN_LITERAL(value)
#define KL1_RELEASE_VERSION_W KL1_RELEASE_WIDEN(KL1_RELEASE_VERSION)
