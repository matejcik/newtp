package desktopsharingprotocol;

public class DSPProtocolException extends DSPRemoteException {
	public DSPProtocolException () {
		super("bad protocol");
		printStackTrace();
	}
}
