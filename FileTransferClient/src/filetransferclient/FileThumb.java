package filetransferclient;

import desktopsharingprotocol.DSPFile;
import java.awt.Color;
import java.awt.event.MouseEvent;
import java.awt.event.MouseListener;
import javax.swing.ImageIcon;
import javax.swing.JLabel;
import javax.swing.SwingUtilities;
import javax.swing.tree.DefaultTreeModel;
import javax.swing.tree.TreePath;

public class FileThumb extends JLabel implements MouseListener {

	private static final ImageIcon folder = new ImageIcon(FileThumb.class.getResource("/images/folder-big.png"));
	private static final ImageIcon document = new ImageIcon(FileThumb.class.getResource("/images/document.png"));

	private static final Color background = new Color(10, 10, 100);
	private Color originalLabel;

	private LoadableNode node;
	private DSPFile file;

	public FileThumb (DirectoryNode node) {
		this.node = node;
		this.file = node.getFile();
		setIcon(folder);
		setText(node.toString());

		setVerticalTextPosition(BOTTOM);
		setHorizontalTextPosition(CENTER);

		setBackground(background);
		originalLabel = getForeground();

		addMouseListener(this);
	}

	public FileThumb (LoadableNode node) {
		this.node = node;
		setIcon(folder);
		setText("..");

		setVerticalTextPosition(BOTTOM);
		setHorizontalTextPosition(CENTER);

		setBackground(background);
		originalLabel = getForeground();

		addMouseListener(this);
	}

	public FileThumb (DSPFile file) {
		this.file = file;
		this.node = null;
		setIcon(document);
		setText(file.getName());

		setVerticalTextPosition(BOTTOM);
		setHorizontalTextPosition(CENTER);

		setBackground(background);
		originalLabel = getForeground();

		addMouseListener(this);
	}

	public void mouseClicked(MouseEvent e) {
		if (node != null &&
			(node instanceof ConnectionNode || ((DirectoryNode)node).getFile().isDirectory())) {
			// descend and open the tree
			DefaultTreeModel model = (DefaultTreeModel)FileBrowser.tree.getModel();
			final TreePath tp = new TreePath(model.getPathToRoot(node));
			if (tp == null) {

			}
			SwingUtilities.invokeLater(new Runnable() {
				public void run() {
					FileBrowser.tree.setSelectionPath(tp);
				}
			});
		}
	}

	public void mousePressed(MouseEvent e) {
		
	}

	public void mouseReleased(MouseEvent e) {
	}

	public void mouseEntered(MouseEvent e) {
		setOpaque(true);
		setForeground(Color.WHITE);
		repaint();
	}

	public void mouseExited(MouseEvent e) {
		setOpaque(false);
		setForeground(originalLabel);
		repaint();
	}
}
