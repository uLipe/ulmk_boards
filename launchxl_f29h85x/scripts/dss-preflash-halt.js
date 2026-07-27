/* dss-preflash-halt.js — leave C29xx_CPU1 halted for DSLite flash.
 *
 * Idle XIP after a prior POR races wr_pll.alg ("Stuck in free-running
 * state").  Prefer halt-without-reset so we do not restart the bad image;
 * fall back to reset+halt if the first halt fails.
 *
 * Usage: dss.sh dss-preflash-halt.js <ccxml> [CPU1]
 */
importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

var ccxml = String(arguments[0]);
var cpu = (arguments.length > 1) ? String(arguments[1]) : "C29xx_CPU1";

var env = ScriptingEnvironment.instance();
env.setScriptTimeout(90000);
var server = env.getServer("DebugServer.1");
server.setConfig(ccxml);
var s = server.openSession("*", cpu);
s.target.connect();

function try_halt(tag)
{
	try {
		s.target.halt();
		System.out.println("dss-preflash-halt: " + tag + " ok");
		return true;
	} catch (e) {
		System.out.println("dss-preflash-halt: " + tag + " " + e);
		return false;
	}
}

if (!try_halt("halt")) {
	try {
		s.target.reset();
	} catch (e) {
		System.out.println("dss-preflash-halt: reset " + e);
	}
	java.lang.Thread.sleep(20);
	try_halt("post-reset halt");
	java.lang.Thread.sleep(20);
	try_halt("post-reset halt2");
}

System.out.println("dss-preflash-halt: parked");
s.target.disconnect();
server.stop();
java.lang.System.exit(0);
