package filetransferclient;

import desktopsharingprotocol.DSPFile;
import java.util.Collections;
import java.util.List;
import javax.swing.JOptionPane;
import javax.swing.JTree;
import javax.swing.SwingUtilities;
import javax.swing.tree.DefaultTreeModel;

public class NodeExpander extends Thread {

	private DefaultTreeModel model;
	private JTree tree;
	private LoadableNode node;
	private Runnable postact;
	private boolean forceRefresh;

	public NodeExpander(LoadableNode node, JTree tree, Runnable postact, boolean forceRefresh) {
		this.node = node;
		this.tree = tree;
		this.model = (DefaultTreeModel)tree.getModel();
		this.postact = postact;
		this.forceRefresh = forceRefresh;
	}

	@Override
	public void run() {
		synchronized(node) {
		if (node.files == null || forceRefresh) {
			System.out.println("expanding : "+node.toString() + "(" + node.hashCode() + ")");
			// loading...
			node.setConnected(false);
			node.removeAllChildren();
			try {
				SwingUtilities.invokeAndWait(new Runnable() {
					public void run() {
						model.nodeStructureChanged(node);
					}
				});
			} catch (Exception e) { }

			try {
				node.list();
				List<DSPFile> list = node.files;
				if (list != null) {
					if (list.get(0).getName().equals(".")) list.remove(0);
					Collections.sort(list);
					for (DSPFile file : list) {
						if (file.isDirectory()) {
							DirectoryNode dn = new DirectoryNode(file);
							System.out.println("inserting : "+dn.toString()+"("+dn.hashCode()+")");
							node.add(new DirectoryNode(file));
						}
					}
				}
			} catch (Exception e) {
				JOptionPane.showMessageDialog(tree,
					e.toString(), "error while connecting",
					JOptionPane.ERROR_MESSAGE);
			} finally {
				node.setConnected(true);
				try {
				SwingUtilities.invokeAndWait(new Runnable() {
					public void run() {
						model.nodeStructureChanged(node);
						if (postact != null) postact.run();
					}
				});
				} catch (Exception e) { }
			}
		} else {
			try {
			if (postact != null) SwingUtilities.invokeAndWait(postact);
			} catch (Exception e) { }
		}
		}
	}
}
