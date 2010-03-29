package desktopsharingprotocol;

public class InvalidPathException extends IllegalArgumentException {
	public InvalidPathException (String reason) {
		super(reason);
	}
}
