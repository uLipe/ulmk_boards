/**
 * DSS: connect → reset → GEL ram_init() → disconnect.
 * Call before loadti on SMP HIL so LPA1/CPA0 have ECC init (loadti only
 * inits M0 in OnPreFileLoaded).
 *
 *   dss.sh dss-raminit.js <ccxml>
 */
importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

var args = this.arguments;
if (args.length < 1) {
	System.err.println("usage: dss-raminit.js <ccxml>");
	java.lang.System.exit(2);
}

var ccxml = String(args[0]);
var env = ScriptingEnvironment.instance();
var server = env.getServer("DebugServer.1");
var session = null;
var rc = 1;

env.setScriptTimeout(120000);

try {
	server.setConfig(ccxml);
	session = server.openSession("*", "C29xx_CPU1");
	session.target.connect();
	session.target.reset();
	System.out.println("dss-raminit: GEL ram_init()");
	session.expression.evaluate("ram_init()");
	System.out.println("dss-raminit: OK");
	session.target.disconnect();
	rc = 0;
} catch (ex) {
	System.err.println("dss-raminit FAIL " + ex);
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
}

java.lang.System.exit(rc);
