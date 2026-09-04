#!/usr/bin/env bash

source "$(dirname "$0")/vars.sh"

if [[ $# -eq 0 ]] ; then
	TEST_SPEC=""
else
	TEST_SPEC="--test-spec $1"
fi

sudo testo run "$SCRIPT_DIR/test_scripts" \
	--stop-on-fail \
	--allowed-sharing-directory "$SCRIPT_DIR" \
	--prefix tt_ \
	--param ISO_DIR "$ISO_DIR" \
	--param OUT_DIR "$OUT_DIR" \
	--param TEST_ASSETS_DIR "$SCRIPT_DIR/test_assets" \
	${TEST_SPEC} \
	--assume-yes \
	--report-folder "$OUT_DIR/allure_report" \
	--report-format allure
