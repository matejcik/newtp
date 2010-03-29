package filetransferclient;

import desktopsharingprotocol.DSPConnection;
import java.io.IOException;
import javax.swing.ImageIcon;

public class ConnectionNode extends LoadableNode {
	private DSPConnection connection;
	private static final ImageIcon defaultIcon = new ImageIcon(ConnectionNode.class.getResource("/images/server.png"));

	public void list() throws IOException {
		files = connection.list("/");
	}

	public ConnectionNode(String name) {
		super(name);
		icon = defaultIcon;
	}

	/**
	 * @return the connection
	 */
	public DSPConnection getConnection() {
		return connection;
	}

	/**
	 * @param connection the connection to set
	 */
	public void setConnection(DSPConnection connection) {
		this.connection = connection;
	}
}
