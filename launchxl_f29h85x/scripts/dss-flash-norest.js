/* dss-flash-norest.js — program flash without pre-op reset (wr_pll safe).
 *
 * Prerequisite: CPU already under debugger control via a prior loadti RAM
 * load (do NOT reset here — reset re-enters bad Main XIP / persistent fault).
 *
 * Usage:
 *   dss.sh dss-flash-norest.js <ccxml> <flashable.out> [ignored] [seccfg=0|1]
 */
importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

var ccxml = String(arguments[0]);
var flashable = String(arguments[1]);
var seccfg = (arguments.length > 3) ? String(arguments[3]) : "0";

var env = ScriptingEnvironment.instance();
env.setScriptTimeout(180000);
var server = env.getServer("DebugServer.1");
server.setConfig(ccxml);
var s = server.openSession("*", "C29xx_CPU1");
s.target.connect();
System.out.println("dss-flash-norest: connected");

try {
	s.options.setBoolean("AutoRunToLabelOnRestart", false);
} catch (e) {}
try {
	s.options.setBoolean("AutoRunToLabelOnReset", false);
} catch (e) {}

/* Best-effort halt; do not reset — that restarts faulty flash XIP. */
try {
	s.target.halt();
	System.out.println("dss-flash-norest: halted");
} catch (e) {
	System.out.println("dss-flash-norest: halt skip " + e);
}

s.flash.options.setBoolean("FlashResetOnOperation", false);
s.flash.options.setString("FlashEraseSelection",
	"Necessary Banks Only (for Program Load)");
s.flash.options.setString("FlashNonMainBankModeBANKMGMT", "0");
s.flash.options.setBoolean("FlashNonMainSECCFGEraseToggle", seccfg === "1");
s.flash.options.setString("FlashDownloadSetting", "Erase and Program");

System.out.println("dss-flash-norest: FlashResetOnOperation=false seccfg=" + seccfg);
System.out.println("dss-flash-norest: programming " + flashable);

try {
	s.memory.loadProgram(flashable);
	System.out.println("dss-flash-norest: OK");
} catch (e) {
	System.err.println("dss-flash-norest: FAIL " + e);
	try { s.target.disconnect(); } catch (e2) {}
	server.stop();
	java.lang.System.exit(1);
}

try { s.target.halt(); } catch (e) {}
s.target.disconnect();
server.stop();
java.lang.System.exit(0);
