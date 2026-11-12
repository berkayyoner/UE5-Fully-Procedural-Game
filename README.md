## Fully Procedural Animated
## Medaival Fantasy RPG Game
### Unreal Engine 5
.
### By Berkay Öner

## Push Commits 20 by 20
> PowerShell: `
# Get all commit hashes in chronological order (oldest first)
$shas  = @(git rev-list --reverse HEAD)
$step  = 20      # Push every 20th commit as a checkpoint
$total = $shas.Count

# Loop through the commits at the given interval and push each checkpoint
for ($i = $step - 1; $i -lt $total; $i += $step) {
    $sha = $shas[$i]
    Write-Host ""
    Write-Host "=== Push $($i+1)/$total : $sha ===" -ForegroundColor Cyan

    # Push this single commit to the remote 'main' branch (force update)
    git push origin "+$sha`:refs/heads/main" --force

    # If the push fails, stop the loop so you can retry with a smaller step
    if ($LASTEXITCODE -ne 0) {
        Write-Host "!! Push failed. Reduce $step to a smaller number and resume from this point." -ForegroundColor Red
        break
    }
}

# Finally, push all remaining commits up to HEAD
Write-Host ""
Write-Host "=== Final push (remaining commits) ===" -ForegroundColor Cyan
git push origin HEAD:refs/heads/main --force
` <br />