// Set only after verifying a project-specific voluntary donation information page.
const donationPage = null;

if (donationPage) {
    const url = new URL(donationPage);
    if (url.protocol === "https:" && !url.username && !url.password) {
        const pendingButton = document.getElementById("donate");
        const link = document.createElement("a");
        link.className = pendingButton.className;
        link.textContent = pendingButton.textContent;
        link.href = url.href;
        pendingButton.replaceWith(link);
        document.getElementById("pending").hidden = true;
    }
}
