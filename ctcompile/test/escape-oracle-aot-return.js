function oracleReturn() { var local = {}; return {child: {}}; }
function oracleLocal() { var local = {}; return 42; }
function oracleCtor() { this.child = {}; return 7; }
function oracleLegacy() { return {}; }
function oracleFailure() { return {}; }
