#!/usr/bin/env bash
#
# RealLive Server API Test Script
# Tests authentication and camera CRUD endpoints
#
# Usage: ./test_api.sh [server_url]
# Default server_url: http://localhost:3000
#

set -euo pipefail

SERVER_URL="${1:-http://localhost:3000}"
API_URL="${SERVER_URL}/api"
PASS=0
FAIL=0
TOTAL=0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Temp files for response data
RESP_BODY=$(mktemp)
RESP_HEADERS=$(mktemp)
trap 'rm -f "$RESP_BODY" "$RESP_HEADERS"' EXIT

##############################################################################
# Helper functions
##############################################################################

log_test() {
    TOTAL=$((TOTAL + 1))
    echo -e "\n${YELLOW}[TEST ${TOTAL}] $1${NC}"
}

assert_status() {
    local expected="$1"
    local actual="$2"
    local test_name="$3"
    if [ "$actual" -eq "$expected" ]; then
        PASS=$((PASS + 1))
        echo -e "  ${GREEN}PASS${NC}: HTTP $actual == $expected - $test_name"
    else
        FAIL=$((FAIL + 1))
        echo -e "  ${RED}FAIL${NC}: HTTP $actual != $expected - $test_name"
        echo "  Response body:"
        cat "$RESP_BODY" | head -5
    fi
}

assert_json_field() {
    local field="$1"
    local expected="$2"
    local actual
    actual=$(jq -r "$field" < "$RESP_BODY" 2>/dev/null || echo "PARSE_ERROR")
    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1))
        echo -e "  ${GREEN}PASS${NC}: $field == \"$expected\""
    else
        FAIL=$((FAIL + 1))
        echo -e "  ${RED}FAIL${NC}: $field == \"$actual\" (expected \"$expected\")"
    fi
}

assert_json_exists() {
    local field="$1"
    local actual
    actual=$(jq -r "$field" < "$RESP_BODY" 2>/dev/null || echo "null")
    if [ "$actual" != "null" ] && [ "$actual" != "" ]; then
        PASS=$((PASS + 1))
        echo -e "  ${GREEN}PASS${NC}: $field exists (value: $actual)"
    else
        FAIL=$((FAIL + 1))
        echo -e "  ${RED}FAIL${NC}: $field is missing or null"
    fi
}

api_request() {
    local method="$1"
    local path="$2"
    local data="${3:-}"
    local token="${4:-}"

    local curl_args=(-s -w "%{http_code}" -o "$RESP_BODY" -D "$RESP_HEADERS")
    curl_args+=(-X "$method")
    curl_args+=(-H "Content-Type: application/json")

    if [ -n "$token" ]; then
        curl_args+=(-H "Authorization: Bearer $token")
    fi

    if [ -n "$data" ]; then
        curl_args+=(-d "$data")
    fi

    curl "${curl_args[@]}" "${API_URL}${path}"
}

# Generate unique test data
TIMESTAMP=$(date +%s)
TEST_USER="testuser_${TIMESTAMP}"
TEST_EMAIL="test_${TIMESTAMP}@example.com"
TEST_PASS="TestPass123!"

##############################################################################
# Wait for server to be ready
##############################################################################

echo "============================================"
echo " RealLive Server API Tests"
echo " Server: ${SERVER_URL}"
echo "============================================"

echo -n "Waiting for server..."
for i in $(seq 1 30); do
    if curl -s "${SERVER_URL}" > /dev/null 2>&1; then
        echo " ready!"
        break
    fi
    if [ "$i" -eq 30 ]; then
        echo -e "\n${RED}ERROR: Server not reachable at ${SERVER_URL}${NC}"
        exit 1
    fi
    echo -n "."
    sleep 1
done

##############################################################################
# S-001: Register with valid credentials
##############################################################################

log_test "S-001: Register with valid credentials"
STATUS=$(api_request POST "/auth/register" \
    "{\"username\":\"${TEST_USER}\",\"email\":\"${TEST_EMAIL}\",\"password\":\"${TEST_PASS}\"}")
assert_status 201 "$STATUS" "Register new user"
assert_json_exists ".user.id"
assert_json_exists ".user.username"

##############################################################################
# S-002: Register with duplicate username
##############################################################################

log_test "S-002: Register with duplicate username"
STATUS=$(api_request POST "/auth/register" \
    "{\"username\":\"${TEST_USER}\",\"email\":\"dup_${TEST_EMAIL}\",\"password\":\"${TEST_PASS}\"}")
assert_status 409 "$STATUS" "Duplicate username rejected"

##############################################################################
# S-003: Register with missing fields
##############################################################################

log_test "S-003: Register with missing fields"
STATUS=$(api_request POST "/auth/register" \
    "{\"username\":\"${TEST_USER}_2\"}")
assert_status 400 "$STATUS" "Missing fields rejected"

##############################################################################
# S-004: Login with valid credentials
##############################################################################

log_test "S-004: Login with valid credentials"
STATUS=$(api_request POST "/auth/login" \
    "{\"username\":\"${TEST_USER}\",\"password\":\"${TEST_PASS}\"}")
assert_status 200 "$STATUS" "Login successful"
assert_json_exists ".token"
TOKEN=$(jq -r '.token' < "$RESP_BODY")

##############################################################################
# S-005: Login with wrong password
##############################################################################

log_test "S-005: Login with wrong password"
STATUS=$(api_request POST "/auth/login" \
    "{\"username\":\"${TEST_USER}\",\"password\":\"WrongPass999\"}")
assert_status 401 "$STATUS" "Wrong password rejected"

##############################################################################
# S-006: Login with non-existent user
##############################################################################

log_test "S-006: Login with non-existent user"
STATUS=$(api_request POST "/auth/login" \
    "{\"username\":\"nonexistent_user_xyz\",\"password\":\"SomePass123\"}")
assert_status 401 "$STATUS" "Non-existent user rejected"

##############################################################################
# S-007: Create camera with valid auth
##############################################################################

log_test "S-007: Create camera with valid auth token"
STATUS=$(api_request POST "/cameras" \
    "{\"name\":\"Test Camera 1\",\"resolution\":\"1080p\"}" "$TOKEN")
assert_status 201 "$STATUS" "Camera created"
assert_json_exists ".camera.id"
assert_json_exists ".camera.stream_key"
assert_json_field ".camera.name" "Test Camera 1"
CAMERA_ID=$(jq -r '.camera.id' < "$RESP_BODY")
STREAM_KEY=$(jq -r '.camera.stream_key' < "$RESP_BODY")

##############################################################################
# S-008: List cameras
##############################################################################

log_test "S-008: List cameras for authenticated user"
STATUS=$(api_request GET "/cameras" "" "$TOKEN")
assert_status 200 "$STATUS" "Camera list returned"
CAMERA_COUNT=$(jq '. | if type == "array" then length elif .cameras then (.cameras | length) else 0 end' < "$RESP_BODY")
if [ "$CAMERA_COUNT" -ge 1 ]; then
    PASS=$((PASS + 1))
    echo -e "  ${GREEN}PASS${NC}: Camera count >= 1 (got $CAMERA_COUNT)"
else
    FAIL=$((FAIL + 1))
    echo -e "  ${RED}FAIL${NC}: Camera count < 1 (got $CAMERA_COUNT)"
fi

##############################################################################
# S-009: Update camera
##############################################################################

log_test "S-009: Update camera name"
STATUS=$(api_request PUT "/cameras/${CAMERA_ID}" \
    "{\"name\":\"Updated Camera Name\"}" "$TOKEN")
assert_status 200 "$STATUS" "Camera updated"
assert_json_field ".camera.name" "Updated Camera Name"

##############################################################################
# S-012: Get stream info
##############################################################################

log_test "S-012: Get stream connection info"
STATUS=$(api_request GET "/cameras/${CAMERA_ID}/stream" "" "$TOKEN")
assert_status 200 "$STATUS" "Stream info returned"

##############################################################################
# S-013: Access protected route without token
##############################################################################

log_test "S-013: Access protected route without token"
STATUS=$(api_request GET "/cameras" "" "")
assert_status 401 "$STATUS" "No token rejected"

##############################################################################
# S-014: Access protected route with invalid token
##############################################################################

log_test "S-014: Access protected route with invalid token"
STATUS=$(api_request GET "/cameras" "" "invalid.jwt.token")
assert_status 401 "$STATUS" "Invalid token rejected"

##############################################################################
# S-011: Delete another user's camera (create second user first)
##############################################################################

log_test "S-011: Cannot delete another user's camera"
TEST_USER2="testuser2_${TIMESTAMP}"
TEST_EMAIL2="test2_${TIMESTAMP}@example.com"
api_request POST "/auth/register" \
    "{\"username\":\"${TEST_USER2}\",\"email\":\"${TEST_EMAIL2}\",\"password\":\"${TEST_PASS}\"}" > /dev/null
STATUS=$(api_request POST "/auth/login" \
    "{\"username\":\"${TEST_USER2}\",\"password\":\"${TEST_PASS}\"}")
TOKEN2=$(jq -r '.token' < "$RESP_BODY")
STATUS=$(api_request DELETE "/cameras/${CAMERA_ID}" "" "$TOKEN2")
assert_status 403 "$STATUS" "Cannot delete other user's camera"

##############################################################################
# S-010: Delete own camera
##############################################################################

log_test "S-010: Delete own camera"
STATUS=$(api_request DELETE "/cameras/${CAMERA_ID}" "" "$TOKEN")
assert_status 200 "$STATUS" "Camera deleted"

# Verify camera is gone
STATUS=$(api_request GET "/cameras/${CAMERA_ID}/stream" "" "$TOKEN")
assert_status 404 "$STATUS" "Deleted camera not found"

##############################################################################
# Summary
##############################################################################

echo ""
echo "============================================"
echo " Test Results"
echo "============================================"
echo -e " Total assertions: $((PASS + FAIL))"
echo -e " ${GREEN}Passed: ${PASS}${NC}"
echo -e " ${RED}Failed: ${FAIL}${NC}"
echo "============================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
