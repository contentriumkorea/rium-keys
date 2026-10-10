#pragma once
#include "owner-command-classifier.h"
// Separate experimental proof. Every fact comes from the same pair of
// identical current-thread snapshots; no supplemental or cached facts.
// No host calls, keys, text or input changes are performed by this evaluator.
bool OwnerMonitorCommand(const OwnerCommandEvidence&);
