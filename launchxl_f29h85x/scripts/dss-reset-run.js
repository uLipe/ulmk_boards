/**
 * DSS: connect → system reset → runAsynch → disconnect.
 *
 * Used after DSLite flash so the C29 boot ROM starts the Main-flash image
 * without reloading an ELF (loadti would reprogram from the .out and is not
 * a POR stand-in).  Leaves no debug session attached.
 *
 * Usage:
 *   dss.sh dss-reset-run.js <path/to.ccxml> [cpuName]
 *
 * cpuName defaults to C29xx_CPU1.
 */
importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

var args = this.arguments;
if (args.length < 1) {
	System.err.println("usage: dss-reset-run.js <ccxml> [cpuName]");
	java.lang.System.exit(2);
}

var ccxml = String(args[0]);
var cpu = (args.length >= 2) ? String(args[1]) : "C29xx_CPU1";
var env = ScriptingEnvironment.instance();
var server = env.getServer("DebugServer.1");
var session = null;
var rc = 1;

env.traceBegin("dss-reset-run", "DSS");
env.setScriptTimeout(90000);

try {
	server.setConfig(ccxml);
	session = server.openSession("*", cpu);
	System.out.println("dss-reset-run: connect " + cpu);
	session.target.connect();
	System.out.println("dss-reset-run: target.reset()");
	session.target.reset();
	System.out.println("dss-reset-run: runAsynch()");
	session.target.runAsynch();
	/* Settle so the core leaves halt-after-reset before we drop the probe. */
	java.lang.Thread.sleep(300);
	System.out.println("dss-reset-run: disconnect");
	session.target.disconnect();
	System.out.println("dss-reset-run: OK");
	rc = 0;
} catch (ex) {
	System.err.println("dss-reset-run: FAIL " + ex);
	try {
		if (session != null)
			session.target.disconnect();
	} catch (ignore) {
	}
	rc = 1;
} finally {
	try {
		server.stop();
	} catch (ignore) {
	}
	env.traceEnd();
}

java.lang.System.exit(rc);
